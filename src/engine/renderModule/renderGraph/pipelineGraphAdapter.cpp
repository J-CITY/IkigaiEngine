#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/gameRenderer.h"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <unordered_map>

namespace IKIGAI::RENDER {
namespace {
	using GRAPH::Access;

	enum class ResourceKind { Texture, StorageTexture, StorageBuffer, UniformBuffer };

	struct ResourceInfo {
		ResourceKind Kind;
		bool ReadOnly = false;
	};

	bool SupportsAccess(ResourceKind kind, Access access) {
		switch (access) {
		case Access::SAMPLED:
		case Access::ATTACHMENT_READ:
		case Access::ATTACHMENT_WRITE:
			return kind == ResourceKind::Texture || kind == ResourceKind::StorageTexture;
		case Access::STORAGE_READ:
		case Access::STORAGE_WRITE:
			return kind == ResourceKind::StorageTexture || kind == ResourceKind::StorageBuffer;
		case Access::INDIRECT:
			return kind == ResourceKind::StorageBuffer;
		case Access::UNIFORM:
			return kind == ResourceKind::UniformBuffer;
		case Access::TRANSFER_READ:
		case Access::TRANSFER_WRITE:
			return kind != ResourceKind::UniformBuffer;
		}
		return false;
	}

	bool HasGraphContract(const PipelineStage::Descriptor& stage) {
		return !stage.ResourceUses.empty() || !stage.After.empty() || stage.SideEffect;
	}

	void RequireUse(const PipelineStage::Descriptor& stage, const std::string& resource, Access access) {
		if (std::none_of(stage.ResourceUses.begin(), stage.ResourceUses.end(),
				[&](const auto& use) { return use.Resource == resource && use.Mode == access; })) {
			throw std::invalid_argument("Missing declared access to " + resource + " in " + stage.Name);
		}
	}
}

GRAPH::Plan RenderGraphPipeline::compileGraph(const Descriptor& descriptor) {
	std::vector<GRAPH::Pass> passes;
	passes.reserve(descriptor.Stages.size());
	if (!descriptor.Graph.Enabled) {
		if (!descriptor.Graph.Imports.empty() || !descriptor.Graph.Exports.empty()
			|| std::any_of(descriptor.Stages.begin(), descriptor.Stages.end(), HasGraphContract)) {
			throw std::invalid_argument("Graph declarations require Graph.Enabled");
		}
		// Legacy stages can touch hidden state, resolve aliases or run on an interval.
		// Treat each as an observable pass and preserve its authored position.
		for (const auto& stage : descriptor.Stages) {
			GRAPH::Pass pass { stage.Name, {}, {}, true };
			if (!passes.empty()) {
				pass.After.push_back(passes.size() - 1);
			}
			passes.push_back(std::move(pass));
		}
		return GRAPH::Compile({}, passes);
	}

	const auto backend = DriverInterface::settings.backend;
	if (backend != RenderSettings::Backend::VULKAN && backend != RenderSettings::Backend::DIRECTX12) {
		throw std::invalid_argument("Explicit render graphs require Vulkan or DirectX 12");
	}
	std::vector<GRAPH::Resource> resources;
	std::vector<ResourceInfo> resourceInfo;
	std::unordered_map<std::string, GRAPH::ResourceId> resourceIds;
	const auto add = [&](const std::string& name, ResourceKind kind, bool imported, bool exported, bool readOnly = false) {
		if (name.empty() || !resourceIds.emplace(name, resources.size()).second) {
			throw std::invalid_argument("Invalid or duplicate render graph resource: " + name);
		}
		resources.push_back({ name, imported, exported });
		resourceInfo.push_back({ kind, readOnly });
	};
	const auto resolve = [&](const std::string& name) {
		const auto found = resourceIds.find(name);
		if (found == resourceIds.end()) {
			throw std::invalid_argument("Unknown render graph resource: " + name);
		}
		return found->second;
	};
	for (const auto& texture : descriptor.Textures) {
		const auto kind = texture.Resource.storage ? ResourceKind::StorageTexture : ResourceKind::Texture;
		add(texture.Name, kind, !texture.Path.empty(), texture.History, !texture.Path.empty());
		if (texture.History) {
			add(texture.Name + "Previous", kind, true, false, true);
		}
	}
	for (const auto& buffer : descriptor.Buffers) {
		add(buffer.Name, buffer.Type == BufferType::UNIFORM ? ResourceKind::UniformBuffer : ResourceKind::StorageBuffer, false, false);
	}
	for (const auto& name : descriptor.Graph.Imports) {
		resources[resolve(name)].Imported = true;
	}
	for (const auto& name : descriptor.Graph.Exports) {
		resources[resolve(name)].Exported = true;
	}

	std::unordered_map<std::string, GRAPH::PassId> passIds;
	for (const auto& stage : descriptor.Stages) {
		if (stage.Name.empty() || !passIds.emplace(stage.Name, passIds.size()).second) {
			throw std::invalid_argument("Invalid or duplicate render graph pass: " + stage.Name);
		}
	}
	if (std::any_of(descriptor.StartStages.begin(), descriptor.StartStages.end(), HasGraphContract)) {
		throw std::invalid_argument("Graph resource uses belong to per-frame stages");
	}

	std::optional<GRAPH::PassId> lastBackbufferPass;
	for (const auto& stage : descriptor.Stages) {
		const bool compute = stage.Draw == DrawContent::COMPUTE;
		const bool raster = stage.Draw == DrawContent::CLEAR || stage.Draw == DrawContent::QUAD || stage.Draw == DrawContent::FORWARD
			|| stage.Draw == DrawContent::DEFERRED || stage.Draw == DrawContent::TRANSPARENT_GEOMETRY || stage.Draw == DrawContent::GUI;
		if ((!raster && !compute) || !stage.Enabled || stage.Once || stage.UpdateInterval != 1 || !stage.Bypass.empty()
			|| stage.Unsupported == "BYPASS" || stage.Name == "CloudShadowMap"
			|| (compute && !stage.Program.empty() && stage.Compute.ProgramKind != ComputeProgramKind::COMPUTE)) {
			throw std::invalid_argument("Stage cannot promise unconditional graph accesses: " + stage.Name);
		}
		const bool writesBackbuffer = raster && stage.FrameBuffer.empty();
		GRAPH::Pass pass { stage.Name, {}, {}, stage.SideEffect || stage.PresentationSink || writesBackbuffer || stage.Draw == DrawContent::GUI };
		if (writesBackbuffer) {
			if (lastBackbufferPass) {
				pass.After.push_back(*lastBackbufferPass);
			}
			lastBackbufferPass = passes.size();
		}
		for (const auto& name : stage.After) {
			const auto found = passIds.find(name);
			if (found == passIds.end()) {
				throw std::invalid_argument("Unknown dependency " + name + " in " + stage.Name);
			}
			pass.After.push_back(found->second);
		}
		for (const auto& use : stage.ResourceUses) {
			const auto id = resolve(use.Resource);
			const auto& info = resourceInfo[id];
			if (!SupportsAccess(info.Kind, use.Mode) || (info.ReadOnly && GRAPH::IsWrite(use.Mode))) {
				throw std::invalid_argument("Incompatible access to " + use.Resource + " in " + stage.Name);
			}
			pass.Uses.push_back({ id, use.Version, use.Mode });
		}
		if (!stage.FrameBuffer.empty()) {
			const auto frame = std::find_if(
				descriptor.FrameBuffers.begin(), descriptor.FrameBuffers.end(), [&](const auto& value) { return value.Name == stage.FrameBuffer; });
			if (!raster || frame == descriptor.FrameBuffers.end() || frame->Mip || frame->Layer) {
				throw std::invalid_argument("Explicit graph needs a whole framebuffer in " + stage.Name);
			}
			const auto attachment = [&](const auto& name, bool clear) {
				const auto texture
					= std::find_if(descriptor.Textures.begin(), descriptor.Textures.end(), [&](const auto& value) { return value.Name == name; });
				if (texture == descriptor.Textures.end()
					|| (texture->Resource.texType != TextureType::TEXTURE_2D && texture->Resource.texType != TextureType::DEPTH)
					|| texture->FullMipChain || texture->Resource.useMipmap || texture->Resource.mipMapCount > 1) {
					throw std::invalid_argument("Partial attachment requires the legacy adapter: " + name);
				}
				RequireUse(stage, name, Access::ATTACHMENT_WRITE);
				if (!clear) {
					RequireUse(stage, name, Access::ATTACHMENT_READ);
				}
			};
			for (const auto& name : frame->Textures) {
				attachment(name, stage.ClearColor);
			}
			if (!frame->Depth.empty()) {
				attachment(frame->Depth, stage.ClearDepth);
			}
		}
		for (const auto& [uniform, value] : stage.Uniforms) {
			if (const auto* name = std::get_if<std::string>(&value)) {
				const bool declared = std::any_of(stage.ResourceUses.begin(), stage.ResourceUses.end(), [&](const auto& use) {
					return use.Resource == *name
						&& (use.Mode == Access::SAMPLED || use.Mode == Access::STORAGE_READ || use.Mode == Access::STORAGE_WRITE);
				});
				if (!declared) {
					throw std::invalid_argument("Missing declared texture access to " + *name + " in " + stage.Name);
				}
			}
		}
		for (const auto& [binding, name] : stage.BufferLinks) {
			if (name == "Lights") {
				continue; // Engine-owned input.
			}
			const auto kind = resourceInfo[resolve(name)].Kind;
			if (kind == ResourceKind::UniformBuffer) {
				RequireUse(stage, name, Access::UNIFORM);
			}
			else if (kind != ResourceKind::StorageBuffer || std::none_of(stage.ResourceUses.begin(), stage.ResourceUses.end(), [&](const auto& use) {
						 return use.Resource == name && (use.Mode == Access::STORAGE_READ || use.Mode == Access::STORAGE_WRITE);
					 })) {
				throw std::invalid_argument("Missing declared buffer access to " + name + " in " + stage.Name);
			}
		}
		for (const auto& name : stage.Reads) {
			if (std::none_of(stage.ResourceUses.begin(), stage.ResourceUses.end(),
					[&](const auto& use) { return use.Resource == name && !GRAPH::IsWrite(use.Mode); })) {
				throw std::invalid_argument("Missing declared read of " + name + " in " + stage.Name);
			}
		}
		for (const auto& name : stage.Writes) {
			RequireUse(stage, name, Access::STORAGE_WRITE);
		}
		if (!stage.Compute.DispatchBuffer.empty()) {
			RequireUse(stage, stage.Compute.DispatchBuffer, Access::INDIRECT);
		}
		if (!stage.Compute.MipChain.Texture.empty()) {
			RequireUse(stage, stage.Compute.MipChain.Texture, Access::STORAGE_WRITE);
			if (stage.Compute.MipChain.FirstMip || stage.Compute.MipChain.MipCount) {
				RequireUse(stage, stage.Compute.MipChain.Texture, Access::STORAGE_READ);
			}
		}
		for (const auto& clear : stage.Compute.ClearBuffers) {
			RequireUse(stage, clear.Buffer, Access::STORAGE_WRITE);
			if (clear.Offset || clear.Size) {
				RequireUse(stage, clear.Buffer, Access::STORAGE_READ);
			}
		}
		passes.push_back(std::move(pass));
	}
	return GRAPH::Compile(resources, passes);
}
}
