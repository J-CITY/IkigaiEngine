#include "profillerWindow.h"
#ifdef USE_EDITOR
#include "imgui.h"
#include "utilsModule/imguiHelper/imgui_widget_flamegraph.h"
#include "profilerModule/profiler.h"
#include "renderModule/diagnostics/renderStatistics.h"
#include "renderModule/gameRenderer.h"
#include "resourceModule/serviceManager.h"
#include "virtualGeometryModule/runtime/virtualGeometryRenderer.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <exception>
#include <functional>
#include <limits>

namespace IKIGAI::EDITOR {
namespace {
struct FlameData { const std::vector<PROFILER::ProfilerReport::Node>* nodes; };
void ProfilerValueGetter(float* start, float* end, ImU8* level, const char** caption, const void* data, int index) {
    const auto& node = (*static_cast<const FlameData*>(data)->nodes)[index];
    if (caption) *caption = node.mName.c_str();
    if (level) *level = static_cast<ImU8>(node.level);
    if (start) *start = static_cast<float>(node.mStart);
    if (end) *end = static_cast<float>(node.mStart + node.mDuration);
}
#if IKIGAI_ENABLE_MEMORY_TRACKING
constexpr std::array Categories{"General", "Nodes", "Components", "Textures", "Meshes", "Models",
    "Shaders", "Materials", "Audio", "Skeletons", "Fonts"};
constexpr std::array Kinds{"Texture", "Buffer", "Vertex buffer", "Index buffer", "Uniform buffer",
    "Storage buffer", "Staging", "Render target", "Acceleration structure", "Shader", "Pipeline"};
constexpr std::array Backends{"Vulkan", "OpenGL", "DX12", "Metal"};
static_assert(Categories.size() == static_cast<std::size_t>(MEMORY::Category::Count));
static_assert(Kinds.size() == static_cast<std::size_t>(PROFILER::GpuResourceKind::Count));
static_assert(Backends.size() == static_cast<std::size_t>(PROFILER::GpuBackend::Count));
std::string Bytes(std::uint64_t bytes) {
    constexpr std::array Units{"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < Units.size()) { value /= 1024.0; ++unit; }
    char text[64];
    if (!unit) std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    else std::snprintf(text, sizeof(text), "%.2f %s", value, Units[unit]);
    return text;
}
void ByteCell(std::uint64_t bytes) {
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(Bytes(bytes).c_str());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%llu bytes", static_cast<unsigned long long>(bytes));
}
void CountCell(std::uint64_t count) {
    ImGui::TableNextColumn();
    ImGui::Text("%llu", static_cast<unsigned long long>(count));
}
void OptionalByteCell(const std::optional<std::uint64_t>& bytes) {
    if (bytes) ByteCell(*bytes);
    else { ImGui::TableNextColumn(); ImGui::TextUnformatted("Unavailable"); }
}
constexpr ImGuiTableFlags SummaryFlags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH
    | ImGuiTableFlags_SizingStretchProp;
constexpr ImGuiTableFlags ResourceFlags = SummaryFlags | ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX
    | ImGuiTableFlags_Resizable;
template<std::size_t N>
bool CategoryCombo(const char* label, int& selection, const std::array<const char*, N>& names) {
    bool changed = false;
    ImGui::SetNextItemWidth(170.0f);
    if (ImGui::BeginCombo(label, selection ? names[selection - 1] : "All")) {
        if (ImGui::Selectable("All", selection == 0)) { selection = 0; changed = true; }
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (ImGui::Selectable(names[i], selection == static_cast<int>(i + 1))) {
                selection = static_cast<int>(i + 1); changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}
#endif
}

void ProfillerWindow::draw() {
    if (!mIsOpen) return;
    ImGui::SetNextWindowSize(ImVec2(1080, 760), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Profiler Window", &mIsOpen)) {
        if (ImGui::BeginTabBar("ProfilerTabs")) {
            if (ImGui::BeginTabItem("Memory")) { drawMemory(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Frame time")) { drawFrameTime(); ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem("Draw statistics")) { drawRenderStatistics(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void ProfillerWindow::drawFrameTime() {
    bool enabled = PROFILER::Profiler::IsEnabled();
    if (ImGui::Checkbox("Record frame time", &enabled)) {
        if (enabled) PROFILER::Profiler::Enable(); else PROFILER::Profiler::Disable();
    }
    const auto& reports = PROFILER::Profiler::getReportHistory();
    const auto& durations = PROFILER::Profiler::getReportHistoryDurations();
    if (reports.empty()) { ImGui::TextUnformatted("No frame samples. Enable recording to collect them."); return; }
    mFrameId = std::min(mFrameId, reports.size() - 1);
    const auto count = static_cast<int>(std::min(durations.size(), static_cast<std::size_t>(std::numeric_limits<int>::max())));
    if (count) {
        const auto selected = ImGuiWidgetFlameGraph::PlotHistogramPressed("Frame history", durations.data(),
            count, 0, nullptr, 0.001f, 1.0f, ImVec2(-1, 80));
        if (selected >= 0 && static_cast<std::size_t>(selected) < reports.size()) mFrameId = selected;
    }
    ImGui::Text("Frame %llu", static_cast<unsigned long long>(mFrameId));
    for (const auto& [thread, nodes] : reports[mFrameId].mCallsTree) {
        if (nodes.empty()) continue;
        const FlameData data{&nodes};
        ImGuiWidgetFlameGraph::PlotFlame("CPU", &ProfilerValueGetter, &data,
            static_cast<int>(nodes.size()), 0, "CPU thread", FLT_MAX, FLT_MAX,
            ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x - 40.0f), 0));
        break;
    }
}

void ProfillerWindow::drawVirtualGeometry() {
    if (!RESOURCES::ServiceManager::Check<RENDER::GameRendererInterface>()) return;
    auto* game = dynamic_cast<RENDER::GameRenderer*>(&RESOURCES::ServiceManager::Get<RENDER::GameRendererInterface>());
    if (!game) return;
    auto& renderer = game->virtualGeometry();
    auto settings = renderer.settings();
    bool changed = ImGui::Checkbox("Virtual geometry", &settings.Enabled);
    ImGui::SameLine(); changed |= ImGui::Checkbox("Freeze LOD", &settings.FreezeLod);
    int mode = static_cast<int>(settings.DebugOverride);
    if (ImGui::Combo("VG debug colours", &mode, "Per component\0Cluster\0LOD level\0Group\0")) {
        settings.DebugOverride = static_cast<ECS::VirtualMeshDebugMode>(mode);
        changed = true;
    }
    changed |= ImGui::SliderFloat("VG threshold scale", &settings.ThresholdScale, 0.0f, 16.0f);
	int path = static_cast<int>(settings.SelectionPath);
	if (ImGui::Combo("VG selection", &path, "Auto\0CPU\0GPU\0")) {
		settings.SelectionPath = static_cast<VG::VirtualGeometrySelectionPath>(path);
		changed = true;
	}
    if (changed) renderer.setSettings(settings);
    const auto& frame = renderer.lastFrame();
    ImGui::Text("VG: %u instances (%u clusters, %u fallback, %u culled), %u visible clusters, %u triangles, %u indirect calls, CPU %.2f ms",
        frame.Instances, frame.ClusterInstances, frame.FallbackInstances, frame.InstancesCulled, frame.VisibleClusters,
        frame.VisibleTriangles, frame.IndirectCalls, frame.SelectionMilliseconds);
	ImGui::Text("VG selection: %s, GPU counters %s (latency %u frames), overflow %u, draw capacity %u, buffers %.1f MB",
		renderer.activeSelectionPath() == VG::VirtualGeometrySelectionPath::GPU ? "GPU" : "CPU", frame.GpuStatisticsValid ? "valid" : "n/a",
		frame.GpuStatisticsLatency, frame.OverflowDraws, frame.DrawCapacity, double(frame.GpuSelectionBytes) / (1024.0 * 1024.0));
}

void ProfillerWindow::drawRenderStatistics() {
    auto& statistics = RENDER::RenderStatistics::Get();
    bool enabled = statistics.isEnabled();
    if (ImGui::Checkbox("Collect draw statistics", &enabled)) statistics.setEnabled(enabled);
    drawVirtualGeometry();
    const auto& frame = statistics.lastFrame();
    if (!enabled || frame.Frames == 0) { ImGui::TextUnformatted("No completed frame. Enable collection to record draw counts."); return; }
    if (ImGui::BeginTable("RenderStatistics", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        for (const char* column : {"Stage", "Draws", "Indirect", "Instances", "Triangles", "Dispatches"}) ImGui::TableSetupColumn(column);
        ImGui::TableHeadersRow();
        const auto row = [](const char* name, const RENDER::RenderDrawCounters& counters) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
            for (const auto value : {counters.DrawCalls, counters.IndirectDraws, counters.Instances, counters.Triangles, counters.Dispatches}) {
                ImGui::TableNextColumn(); ImGui::Text("%llu", static_cast<unsigned long long>(value));
            }
        };
        row("Frame total", frame.Total);
        for (const auto& stage : frame.Stages) row(stage.Name.c_str(), stage.Counters);
        ImGui::EndTable();
    }
    const auto& vg = frame.VirtualGeometry;
    if (vg.ClusterInstances || vg.FallbackInstances) {
        ImGui::Text("Virtual geometry: %llu cluster instances, %llu fallback instances, %llu visible clusters, %llu triangles",
            static_cast<unsigned long long>(vg.ClusterInstances), static_cast<unsigned long long>(vg.FallbackInstances),
            static_cast<unsigned long long>(vg.VisibleClusters), static_cast<unsigned long long>(vg.VisibleTriangles));
    }
}

bool ProfillerWindow::captureMemorySnapshot() {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    try {
        auto snapshot = PROFILER::MemoryProfiler::instance().captureSnapshot();
        std::sort(snapshot.allocations.begin(), snapshot.allocations.end(), [](const auto& a, const auto& b) {
            return a.bytes != b.bytes ? a.bytes > b.bytes : std::less<const void*>{}(a.address, b.address);
        });
        std::sort(snapshot.gpu.allocations.begin(), snapshot.gpu.allocations.end(), [](const auto& a, const auto& b) {
            return a.info.bytes != b.info.bytes ? a.info.bytes > b.info.bytes : a.id < b.id;
        });
        mMemorySnapshot = std::move(snapshot);
        mCapturedAt = std::chrono::steady_clock::now();
        mMemoryError.clear();
        rebuildMemoryRows();
        return true;
    } catch (const std::exception& error) { mMemoryError = error.what(); }
    catch (...) { mMemoryError = "Snapshot capture failed."; }
#endif
    return false;
}
void ProfillerWindow::clearMemorySnapshot() {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    mMemorySnapshot.reset(); mCpuRows.clear(); mGpuRows.clear(); mMemoryError.clear();
#endif
}
const PROFILER::MemorySnapshot* ProfillerWindow::memorySnapshot() const {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    return mMemorySnapshot ? &*mMemorySnapshot : nullptr;
#else
    return nullptr;
#endif
}

void ProfillerWindow::drawMemory() {
#if IKIGAI_ENABLE_MEMORY_TRACKING
    if (ImGui::Button("Capture snapshot")) captureMemorySnapshot();
    ImGui::SameLine();
    ImGui::BeginDisabled(!mMemorySnapshot);
    if (ImGui::Button("Clear snapshot")) clearMemorySnapshot();
    ImGui::EndDisabled();
    if (!mMemoryError.empty()) ImGui::TextWrapped("Snapshot error: %s", mMemoryError.c_str());
    if (!mMemorySnapshot) {
        ImGui::TextWrapped("Capture a snapshot to inspect tracked CPU and GPU allocations. Data is collected only on request.");
        return;
    }
    const auto age = std::chrono::duration<double>(std::chrono::steady_clock::now() - mCapturedAt).count();
    ImGui::Text("Snapshot age: %.1f s. Data stays fixed until the next capture.", age);
    const auto& snapshot = *mMemorySnapshot;
    if (ImGui::CollapsingHeader("Process / system RAM")) {
        if (ImGui::BeginTable("ProcessMemory", 2, SummaryFlags)) {
            const auto metric = [](const char* name, const auto& value) {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(name); OptionalByteCell(value);
            };
            metric("Process resident / working set", snapshot.process.residentBytes);
            metric("Peak resident / working set", snapshot.process.peakResidentBytes);
            metric("Private committed (Windows)", snapshot.process.privateCommittedBytes);
            metric("Physical footprint (Apple)", snapshot.process.physicalFootprintBytes);
            metric("System physical RAM", snapshot.process.totalPhysicalBytes);
            metric("System available RAM", snapshot.process.availablePhysicalBytes);
            ImGui::EndTable();
        }
        ImGui::TextWrapped("OS counters cover the process/system. They overlap with tracked allocations and must not be added to them.");
    }
    if (snapshot.droppedRecords || snapshot.gpu.droppedRecords || snapshot.gpu.failedSamplers) {
        ImGui::TextWrapped("Incomplete snapshot: CPU dropped %llu, GPU dropped %llu, failed heap sources %llu.",
            static_cast<unsigned long long>(snapshot.droppedRecords), static_cast<unsigned long long>(snapshot.gpu.droppedRecords),
            static_cast<unsigned long long>(snapshot.gpu.failedSamplers));
    }
    if (mMemoryFilter.Draw("Search", 320.0f)) rebuildMemoryRows();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Search categories, resource names, backend, IDs or hexadecimal addresses. Commas mean OR; - excludes a term.");
    ImGui::TextWrapped("CPU/GPU lists show connected engine allocations. OS counters are separate. Byte totals exclude resources with unavailable sizes.");
    if (ImGui::BeginTabBar("MemoryDomains")) {
        if (ImGui::BeginTabItem("CPU allocations")) { drawCpuMemory(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("GPU allocations")) { drawGpuMemory(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
#else
    ImGui::TextWrapped("Memory profiling is disabled in this build. Configure CMake with IKIGAI_ENABLE_MEMORY_TRACKING=ON and rebuild.");
#endif
}

#if IKIGAI_ENABLE_MEMORY_TRACKING
void ProfillerWindow::rebuildMemoryRows() {
    mCpuRows.clear(); mGpuRows.clear();
    if (!mMemorySnapshot) return;
    const auto& snapshot = *mMemorySnapshot;
    for (std::size_t i = 0; i < snapshot.allocations.size(); ++i) {
        const auto& record = snapshot.allocations[i];
        if (mCpuCategory && static_cast<int>(record.category) != mCpuCategory - 1) continue;
        char text[160];
        std::snprintf(text, sizeof(text), "%s %p %p", Categories[static_cast<std::size_t>(record.category)], record.address, record.allocator);
        if (mMemoryFilter.PassFilter(text)) mCpuRows.push_back(i);
    }
    for (std::size_t i = 0; i < snapshot.gpu.allocations.size(); ++i) {
        const auto& record = snapshot.gpu.allocations[i];
        const auto& info = record.info;
        if (mGpuKind && static_cast<int>(info.kind) != mGpuKind - 1) continue;
        if (mGpuBackend && static_cast<int>(info.backend) != mGpuBackend - 1) continue;
        if (mPendingOnly && !record.pendingRelease) continue;
        char text[192];
        std::snprintf(text, sizeof(text), "%s %s %llu 0x%llx", Kinds[static_cast<std::size_t>(info.kind)],
            Backends[static_cast<std::size_t>(info.backend)], static_cast<unsigned long long>(record.id),
            static_cast<unsigned long long>(info.handle));
        const auto searchable = record.name + " " + text;
        if (mMemoryFilter.PassFilter(searchable.c_str())) mGpuRows.push_back(i);
    }
}

void ProfillerWindow::drawCpuMemory() {
    const auto& snapshot = *mMemorySnapshot;
    std::uint64_t bytes = 0, allocations = 0;
    for (const auto& total : snapshot.categories) { bytes += total.bytes; allocations += total.allocations; }
    ImGui::Text("Tracked CPU: %s in %llu allocations", Bytes(bytes).c_str(), static_cast<unsigned long long>(allocations));
    if (ImGui::CollapsingHeader("CPU categories", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("CpuCategories", 4, SummaryFlags)) {
            ImGui::TableSetupColumn("Category"); ImGui::TableSetupColumn("Requested");
            ImGui::TableSetupColumn("Peak"); ImGui::TableSetupColumn("Allocations"); ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < Categories.size(); ++i) {
                const auto& total = snapshot.categories[i];
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(Categories[i]);
                ByteCell(total.bytes); ByteCell(total.peakBytes); CountCell(total.allocations);
            }
            ImGui::EndTable();
        }
    }
    if (CategoryCombo("Category", mCpuCategory, Categories)) rebuildMemoryRows();
    ImGui::Text("%llu / %llu allocations, largest first", static_cast<unsigned long long>(mCpuRows.size()),
        static_cast<unsigned long long>(snapshot.allocations.size()));
    if (ImGui::BeginTable("CpuAllocations", 5, ResourceFlags, ImVec2(0, std::max(140.0f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn("Category", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::TableSetupColumn("Requested", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Allocator", ImGuiTableColumnFlags_WidthFixed, 150);
        ImGui::TableSetupColumn("Alignment", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
        ImGuiListClipper clipper; clipper.Begin(static_cast<int>(mCpuRows.size()));
        while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto& record = snapshot.allocations[mCpuRows[row]];
            ImGui::TableNextRow(); ImGui::TableNextColumn();
            ImGui::TextUnformatted(Categories[static_cast<std::size_t>(record.category)]);
            ByteCell(record.bytes);
            ImGui::TableNextColumn(); ImGui::Text("%p", record.address);
            ImGui::TableNextColumn(); ImGui::Text("%p", record.allocator);
            ImGui::TableNextColumn();
            if (record.alignment) ImGui::Text("%llu", static_cast<unsigned long long>(record.alignment));
            else ImGui::TextUnformatted("Default");
        }
        ImGui::EndTable();
    }
}

void ProfillerWindow::drawGpuMemory() {
    const auto& gpu = mMemorySnapshot->gpu;
    if (!gpu.trackingEnabled) { ImGui::TextUnformatted("GPU tracking is unavailable in this snapshot."); return; }
    ImGui::Text("Allocator-reported: %s    Estimated storage: %s", Bytes(gpu.totals.reportedBytes).c_str(), Bytes(gpu.totals.estimatedBytes).c_str());
    ImGui::Text("Pending release: %s in %llu allocations (included in totals)", Bytes(gpu.totals.pendingBytes).c_str(),
        static_cast<unsigned long long>(gpu.totals.pendingAllocations));
    ImGui::Text("Unknown size: %llu resources. Resident VRAM: unavailable.", static_cast<unsigned long long>(gpu.totals.unknownSizeAllocations));
    if (ImGui::CollapsingHeader("GPU backends", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("GpuBackends", 6, SummaryFlags)) {
            ImGui::TableSetupColumn("Backend"); ImGui::TableSetupColumn("Reported");
            ImGui::TableSetupColumn("Estimated"); ImGui::TableSetupColumn("Tracked peak");
            ImGui::TableSetupColumn("Unknown size"); ImGui::TableSetupColumn("Resources"); ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < Backends.size(); ++i) {
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(Backends[i]);
                if (!gpu.allocationTrackingAvailable[i]) {
                    ImGui::TableNextColumn(); ImGui::TextUnformatted("Unavailable"); continue;
                }
                const auto& total = gpu.backends[i];
                ByteCell(total.reportedBytes); ByteCell(total.estimatedBytes); ByteCell(total.peakBytes);
                CountCell(total.unknownSizeAllocations); CountCell(total.allocations);
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("GPU resource categories")) {
        if (ImGui::BeginTable("GpuKinds", 6, SummaryFlags)) {
            ImGui::TableSetupColumn("Type"); ImGui::TableSetupColumn("Reported"); ImGui::TableSetupColumn("Estimated");
            ImGui::TableSetupColumn("Pending"); ImGui::TableSetupColumn("Unknown size");
            ImGui::TableSetupColumn("Resources"); ImGui::TableHeadersRow();
            for (std::size_t i = 0; i < Kinds.size(); ++i) {
                const auto& total = gpu.kinds[i];
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(Kinds[i]);
                ByteCell(total.reportedBytes); ByteCell(total.estimatedBytes); ByteCell(total.pendingBytes);
                CountCell(total.unknownSizeAllocations); CountCell(total.allocations);
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("GPU heaps / device memory")) {
        ImGui::TextWrapped("Allocation bytes are contained in reserved blocks. Usage and budget are estimates. Do not add these columns together.");
        ImGui::TextWrapped("Metal shows device resource allocations and recommended working set in the last two size columns; these are not residency or an OS-assigned budget.");
        if (gpu.heaps.empty()) ImGui::TextUnformatted("No heap data from this backend.");
        else if (ImGui::BeginTable("GpuHeaps", 7, SummaryFlags | ImGuiTableFlags_ScrollX,
            ImVec2(0, std::min(220.0f, (static_cast<float>(gpu.heaps.size()) + 1) *
                (ImGui::GetTextLineHeight() + 2 * ImGui::GetStyle().CellPadding.y) + ImGui::GetStyle().ScrollbarSize + 4)))) {
            ImGui::TableSetupColumn("Heap"); ImGui::TableSetupColumn("Capacity"); ImGui::TableSetupColumn("Allocated");
            ImGui::TableSetupColumn("Reserved"); ImGui::TableSetupColumn("Usage / allocated"); ImGui::TableSetupColumn("Budget / recommended");
            ImGui::TableSetupColumn("Source"); ImGui::TableHeadersRow();
            for (const auto& heap : gpu.heaps) {
                ImGui::TableNextRow(); ImGui::TableNextColumn();
                if (heap.backend == PROFILER::GpuBackend::Metal) ImGui::TextUnformatted("Metal device");
                else ImGui::Text("%u (%s)", heap.heap, heap.deviceLocal ? "device local" : "shared / host");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s domain 0x%llx", Backends[static_cast<std::size_t>(heap.backend)], static_cast<unsigned long long>(heap.device));
                OptionalByteCell(heap.capacityAvailable ? std::optional(heap.capacityBytes) : std::nullopt);
                OptionalByteCell(heap.allocatorStatsAvailable ? std::optional(heap.allocationBytes) : std::nullopt);
                OptionalByteCell(heap.allocatorStatsAvailable ? std::optional(heap.reservedBytes) : std::nullopt);
                OptionalByteCell(heap.deviceAllocatedBytes ? heap.deviceAllocatedBytes : heap.usageAvailable ? std::optional(heap.processUsageBytes) : std::nullopt);
                OptionalByteCell(heap.recommendedWorkingSetBytes ? heap.recommendedWorkingSetBytes : heap.budgetAvailable ? std::optional(heap.budgetBytes) : std::nullopt);
                ImGui::TableNextColumn();
                const auto source = heap.backend == PROFILER::GpuBackend::Metal ? "Metal device" :
                    heap.backend == PROFILER::GpuBackend::DirectX12 ? (heap.driverBudget ? "DXGI" : "D3D12MA fallback") :
                    heap.driverBudget ? "Driver budget" : "VMA fallback";
                ImGui::TextUnformatted(source);
                if (heap.unifiedMemory && ImGui::IsItemHovered()) ImGui::SetTooltip("Unified memory: %s", *heap.unifiedMemory ? "yes" : "no");
            }
            ImGui::EndTable();
        }
    }
    bool changed = CategoryCombo("Type", mGpuKind, Kinds);
    ImGui::SameLine(); changed |= CategoryCombo("Backend", mGpuBackend, Backends);
    changed |= ImGui::Checkbox("Pending only", &mPendingOnly);
    if (changed) rebuildMemoryRows();
    ImGui::Text("%llu / %llu resources, largest first", static_cast<unsigned long long>(mGpuRows.size()),
        static_cast<unsigned long long>(gpu.allocations.size()));
    if (ImGui::BeginTable("GpuAllocations", 7, ResourceFlags, ImVec2(0, std::max(140.0f, ImGui::GetContentRegionAvail().y)))) {
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 55);
        ImGui::TableSetupColumn("Name / handle", ImGuiTableColumnFlags_WidthFixed, 270);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 130);
        ImGui::TableSetupColumn("Backend", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Measurement", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
        ImGuiListClipper clipper; clipper.Begin(static_cast<int>(mGpuRows.size()));
        while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const auto& record = gpu.allocations[mGpuRows[row]];
            const auto& info = record.info;
            ImGui::TableNextRow(); CountCell(record.id);
            ImGui::TableNextColumn();
            if (record.name.empty()) ImGui::Text("0x%llx", static_cast<unsigned long long>(info.handle));
            else ImGui::TextUnformatted(record.name.c_str());
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                if (!record.name.empty()) ImGui::TextUnformatted(record.name.c_str());
                ImGui::Text("Handle 0x%llx; domain 0x%llx", static_cast<unsigned long long>(info.handle), static_cast<unsigned long long>(info.device));
                if (info.heap != UINT32_MAX) ImGui::Text("Heap %u; device-local: %s; host-visible: %s", info.heap, info.deviceLocal ? "yes" : "no", info.hostVisible ? "yes" : "no");
                if (info.requestedBytes) ImGui::Text("Requested: %s", Bytes(info.requestedBytes).c_str());
                ImGui::EndTooltip();
            }
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Kinds[static_cast<std::size_t>(info.kind)]);
            ImGui::TableNextColumn(); ImGui::TextUnformatted(Backends[static_cast<std::size_t>(info.backend)]);
            OptionalByteCell(info.measurement == PROFILER::GpuMeasurement::Unavailable ? std::nullopt : std::optional(info.bytes));
            ImGui::TableNextColumn(); ImGui::TextUnformatted(info.measurement == PROFILER::GpuMeasurement::ReportedAllocation ? "Reported" :
                info.measurement == PROFILER::GpuMeasurement::EstimatedStorage ? "Estimated" : "Unavailable");
            ImGui::TableNextColumn(); ImGui::TextUnformatted(record.pendingRelease ? "Pending" : "Live");
        }
        ImGui::EndTable();
    }
}
#endif
}
#endif
