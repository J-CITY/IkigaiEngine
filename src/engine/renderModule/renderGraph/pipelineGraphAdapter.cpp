#include "computeStage.h"
#include "renderModule/backends/interface/driverInterface.h"
#include "renderModule/gameRenderer.h"
#include <algorithm>
#include <iostream>
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
		bool QueueImage = false;
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
		return !stage.ResourceUses.empty() || !stage.After.empty() || stage.SideEffect || stage.Queue != GRAPH::Queue::GRAPHICS;
	}

	void RequireUse(const PipelineStage::Descriptor& stage, const std::string& resource, Access access) {
		if (std::none_of(stage.ResourceUses.begin(), stage.ResourceUses.end(),
				[&](const auto& use) { return use.Resource == resource && use.Mode == access; })) {
			throw std::invalid_argument("Missing declared access to " + resource + " in " + stage.Name);
		}
	}

	bool IsImplicitEngineTexture(std::string_view name) {
		// These pipeline names are also accessed through implicit engine bindings.
		constexpr std::string_view engineTextures[] = { "ShapeNoise", "DetailNoise", "CurlNoise", "CloudShadowMap", "AtmosphereTransmittance",
			"AtmosphereSkyView", "AtmosphereAerial", "CloudDepth", "Display" };
		return std::find(std::begin(engineTextures), std::end(engineTextures), name) != std::end(engineTextures);
	}

	bool IsTransientColor(const RenderGraphPipeline::TextureDesc& texture) {
		return texture.Transient && !texture.Persistent && !texture.History && texture.Path.empty() && !texture.FullMipChain
			&& texture.Resource.texType == TextureType::TEXTURE_2D && !texture.Resource.storage && !texture.Resource.useMipmap
			&& texture.Resource.mipMapCount <= 1 && texture.Resource.colorData.empty() && texture.Resource.pathTexture.empty()
			&& !IsImplicitEngineTexture(texture.Name);
	}

	bool BindsTexture(const PipelineStage::Descriptor& pass, const std::string& resource) {
		return std::ranges::any_of(pass.Uniforms, [&](const auto& uniform) {
			const auto* name = std::get_if<std::string>(&uniform.second);
			return name && *name == resource;
		});
	}

	bool HasSoleReader(
		const RenderGraphPipeline::Descriptor& descriptor, const GRAPH::Barrier& barrier, GRAPH::PassId reader, const std::string& resource) {
		const auto& writer = descriptor.Stages[barrier.Before->Pass];
		const auto write = std::ranges::find_if(
			writer.ResourceUses, [&](const auto& use) { return use.Resource == resource && use.Mode == barrier.Before->Mode; });
		if (write == writer.ResourceUses.end()) {
			return false;
		}
		std::optional<GRAPH::PassId> found;
		for (GRAPH::PassId id = 0; id < descriptor.Stages.size(); ++id) {
			if (std::ranges::any_of(descriptor.Stages[id].ResourceUses,
					[&](const auto& use) { return use.Resource == resource && use.Version == write->Version && !GRAPH::IsWrite(use.Mode); })) {
				if (found) {
					return false;
				}
				found = id;
			}
		}
		return found == reader;
	}

	std::optional<GraphImageBarrier> ResolveImageTransition(const RenderGraphPipeline::Descriptor& descriptor, const GRAPH::Barrier& barrier,
		GRAPH::PassId consumerId, const PipelineStage& loadedConsumer, const std::string& resource) {
		if (!barrier.Before) {
			return {};
		}
		const auto texture = std::ranges::find(descriptor.Textures, resource, &RenderGraphPipeline::TextureDesc::Name);
		if (texture == descriptor.Textures.end() || texture->History || !texture->Path.empty()) {
			return {};
		}
		const auto& producer = descriptor.Stages[barrier.Before->Pass];
		const auto& consumer = descriptor.Stages[consumerId];
		const auto sampledAccess = [&]() -> std::optional<GraphImageAccess> {
			if (barrier.After != Access::SAMPLED || !BindsTexture(consumer, resource) || consumer.Material.empty()) {
				return {};
			}
			if (consumer.Draw == DrawContent::COMPUTE && consumer.Program.empty()) {
				return GraphImageAccess::ComputeSampledRead;
			}
			if (consumer.Draw == DrawContent::QUAD) {
				return GraphImageAccess::GraphicsSampledRead;
			}
			return {};
		}();
		if (!HasSoleReader(descriptor, barrier, consumerId, resource)) {
			// A later reader can use another shader stage or a different layout.
			// The compiler does not yet track visibility separately for each reader.
			return {};
		}
		if (barrier.Before->Mode == Access::ATTACHMENT_WRITE && sampledAccess && texture->Resource.texType == TextureType::DEPTH) {
			const auto framebuffer = std::ranges::find(descriptor.FrameBuffers, producer.FrameBuffer, &RenderGraphPipeline::FrameDesc::Name);
			if (framebuffer != descriptor.FrameBuffers.end() && framebuffer->Depth == resource) {
				return GraphImageBarrier { GraphImageAccess::DepthAttachmentWrite, *sampledAccess, GraphImageAspect::Depth, 0, 1, 0, 1 };
			}
		}
		if (barrier.Before->Mode != Access::STORAGE_WRITE || !texture->Resource.storage || texture->Resource.texType != TextureType::TEXTURE_2D
			|| producer.Draw != DrawContent::COMPUTE || !producer.Program.empty()) {
			return {};
		}
		const auto& inputChain = consumer.Compute.MipChain;
		if (barrier.After == Access::STORAGE_WRITE && producer.Compute.MipChain.Texture.empty() && BindsTexture(producer, resource)
			&& inputChain.Texture == resource && inputChain.FirstMip == 1 && !inputChain.Input.empty() && loadedConsumer.mCompute
			&& loadedConsumer.mCompute->MipScopedBarriers) {
			// The default storage view writes mip 0. The in-place chain reads it and writes mip 1 onward.
			return GraphImageBarrier { GraphImageAccess::StorageWrite, GraphImageAccess::StorageRead, GraphImageAspect::Color, 0, 1, 0, 1 };
		}
		const auto& outputChain = producer.Compute.MipChain;
		if (sampledAccess && outputChain.Texture == resource && outputChain.FirstMip == 1 && !outputChain.Input.empty() && !outputChain.MipCount) {
			// The sampled view can fetch any mip, including the retained mip 0.
			return GraphImageBarrier { GraphImageAccess::StorageWrite, *sampledAccess, GraphImageAspect::Color };
		}
		return {};
	}
}

GRAPH::Plan RenderGraphPipeline::compileGraph(const Descriptor& descriptor) {
	std::vector<GRAPH::Pass> passes;
	passes.reserve(descriptor.Stages.size());
	if (!descriptor.Graph.Enabled) {
		if (descriptor.Graph.Aliasing || !descriptor.Graph.Imports.empty() || !descriptor.Graph.Exports.empty()
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
		resourceInfo.back().QueueImage = texture.Resource.texType == TextureType::TEXTURE_2D && !texture.FullMipChain && !texture.History
			&& !texture.Resource.useMipmap && texture.Resource.mipMapCount <= 1 && texture.Resource.pathTexture.empty()
			&& texture.Resource.colorData.empty() && !IsImplicitEngineTexture(texture.Name);
		resources.back().Transient = descriptor.StartStages.empty() && descriptor.LightPasses.empty() && IsTransientColor(texture);
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
	std::vector<bool> asyncImages(resources.size());
	for (const auto& stage : descriptor.Stages) {
		if (stage.Queue == GRAPH::Queue::ASYNC_COMPUTE) {
			for (const auto& use : stage.ResourceUses) {
				const auto id = resolve(use.Resource);
				asyncImages[id] = resourceInfo[id].QueueImage;
			}
		}
	}

	std::optional<GRAPH::PassId> lastBackbufferPass;
	for (const auto& stage : descriptor.Stages) {
		const bool compute = stage.Draw == DrawContent::COMPUTE;
		const bool raster = stage.Draw == DrawContent::CLEAR || stage.Draw == DrawContent::QUAD || stage.Draw == DrawContent::FORWARD
			|| stage.Draw == DrawContent::DEFERRED || stage.Draw == DrawContent::TRANSPARENT_GEOMETRY || stage.Draw == DrawContent::GUI
			|| stage.Draw == DrawContent::GPU_SCENE_OCCLUSION_POST || stage.Draw == DrawContent::VIRTUAL_GEOMETRY_VISIBILITY
			|| stage.Draw == DrawContent::VIRTUAL_GEOMETRY_RESOLVE;
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
			const bool visibilityClear = stage.Draw == DrawContent::VIRTUAL_GEOMETRY_VISIBILITY;
			for (const auto& name : frame->Textures) {
				attachment(name, stage.ClearColor || visibilityClear);
				if (visibilityClear) {
					pass.ClearedColorAttachments.push_back(resolve(name));
				}
			}
			if (stage.ClearColor && frame->Textures.size() == 1 && frame->Depth.empty()
				&& (stage.Draw == DrawContent::CLEAR || stage.Draw == DrawContent::QUAD)) {
				pass.ClearedColorAttachments.push_back(resolve(frame->Textures.front()));
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
		if (stage.Draw == DrawContent::GPU_SCENE_OCCLUSION_POST) {
			RequireUse(stage, stage.Bindings.at("Hzb"), Access::SAMPLED);
		}
		if (stage.Draw == DrawContent::VIRTUAL_GEOMETRY_RESOLVE) {
			RequireUse(stage, stage.Bindings.at("Visibility"), Access::SAMPLED);
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
		const bool requestedAsync = stage.Queue == GRAPH::Queue::ASYNC_COMPUTE;
		bool asyncProgram
			= compute && !stage.Program.empty() && stage.Compute.ClearBuffers.empty() && !stage.SideEffect && !stage.ResourceUses.empty();
		bool hasImage = false;
		bool hasArray = false;
		std::vector<bool> bound(requestedAsync ? resources.size() : 0);
		const auto queueImage = [&](GRAPH::ResourceId id) { return resourceInfo[id].QueueImage && !resources[id].Imported; };
		const auto queueAccess = [&](const GRAPH::Use& use) {
			const auto& info = resourceInfo[use.Resource];
			if (use.Mode == Access::SAMPLED) {
				return queueImage(use.Resource);
			}
			return (use.Mode == Access::STORAGE_READ || use.Mode == Access::STORAGE_WRITE)
				&& (info.Kind == ResourceKind::StorageBuffer || (info.Kind == ResourceKind::StorageTexture && queueImage(use.Resource)));
		};
		for (const auto& [binding, sources] : stage.Bindings) {
			hasArray |= sources.find(',') != std::string::npos;
			size_t begin = 0;
			do {
				const auto end = sources.find(',', begin);
				const auto source = sources.substr(begin, end == std::string::npos ? end : end - begin);
				const auto found = resourceIds.find(source);
				if (found != resourceIds.end()) {
					// Program bindings can perform native accesses outside raster attachment/sampler reflection.
					resources[found->second].Transient = false;
					if (asyncImages[found->second] && !std::ranges::any_of(pass.Uses, [&](const auto& use) {
							return use.Resource == found->second
								&& (use.Mode == Access::SAMPLED || use.Mode == Access::STORAGE_READ || use.Mode == Access::STORAGE_WRITE);
						})) {
						throw std::invalid_argument("Missing declared access to queue image " + source + " in " + stage.Name);
					}
				}
				if (requestedAsync) {
					if (found == resourceIds.end()) {
						asyncProgram = false;
					}
					else {
						const auto id = found->second;
						const bool image = queueImage(id);
						hasImage |= image;
						asyncProgram &= !(image && bound[id])
							&& std::ranges::any_of(pass.Uses, [&](const auto& use) { return use.Resource == id && queueAccess(use); });
						bound[id] = true;
					}
				}
				if (end == std::string::npos) {
					break;
				}
				begin = end + 1;
			} while (true);
		}
		if (requestedAsync) {
			if (!asyncProgram || stage.Bindings.empty() || (hasImage && hasArray)
				|| !std::ranges::all_of(pass.Uses, [&](const auto& use) { return bound[use.Resource] && queueAccess(use); })) {
				throw std::invalid_argument(
					"Async compute requires a reflected compute program with declared storage buffers or local single-mip 2D images: " + stage.Name);
			}
			if (descriptor.Execution.AsyncCompute) {
				pass.PreferredQueue = stage.Queue;
			}
		}
		passes.push_back(std::move(pass));
	}
	return GRAPH::Compile(resources, passes);
}

std::vector<RenderGraphPipeline::ResolvedGraphBarriers> RenderGraphPipeline::resolveGraphBarriers(
	const Descriptor& descriptor, const GRAPH::Plan& plan) const {
	std::vector<ResolvedGraphBarriers> result(descriptor.Stages.size());
	std::vector<GRAPH::Queue> queues(descriptor.Stages.size(), GRAPH::Queue::GRAPHICS);
	for (const auto& pass : plan.Schedule) {
		queues[pass.Id] = pass.ExecutionQueue;
		auto& resolved = result[pass.Id];
		for (const auto& barrier : pass.Barriers) {
			const auto& name = plan.ResourceNames[barrier.Resource];
			const auto image = mTextures.find(name);
			if (image != mTextures.end() && barrier.Before && queues[barrier.Before->Pass] == GRAPH::Queue::GRAPHICS
				&& pass.ExecutionQueue == GRAPH::Queue::GRAPHICS) {
				if (auto transition = ResolveImageTransition(descriptor, barrier, pass.Id, *mStageIndex[pass.Id], name)) {
					resolved.Images.push_back({ &image->second, *transition });
					continue;
				}
			}
			// Cross-queue and undeclared native uses still follow the conservative path.
			if (barrier.Before && barrier.Before->Mode == Access::STORAGE_WRITE && barrier.After == Access::INDIRECT
				&& descriptor.Stages[barrier.Before->Pass].Draw == DrawContent::COMPUTE && queues[barrier.Before->Pass] == GRAPH::Queue::GRAPHICS
				&& pass.ExecutionQueue == GRAPH::Queue::GRAPHICS) {
				const auto usage = mBufferUsages.find(name);
				const bool onlyIndirectReaders = std::ranges::none_of(descriptor.Stages, [&](const auto& stage) {
					return std::ranges::any_of(stage.ResourceUses,
						[&](const auto& use) { return use.Resource == name && use.Mode != Access::STORAGE_WRITE && use.Mode != Access::INDIRECT; });
				});
				if (onlyIndirectReaders && usage != mBufferUsages.end() && HasBufferUsage(usage->second, BufferUsage::INDIRECT)) {
					resolved.IndirectBuffers.push_back(mStorageBuffers.at(name));
					continue;
				}
			}
			resolved.Conservative = true;
			resolved.Uav |= barrier.Uav;
		}
		if (!resolved.Images.empty()) {
			std::cout << "GRAPH_IMAGE_BARRIERS pass=" << descriptor.Stages[pass.Id].Name << " count=" << resolved.Images.size()
					  << " conservative=" << resolved.Conservative << '\n';
		}
	}
	return result;
}
}
