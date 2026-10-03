#include "statWindow.h"

#include "utilsModule/animation.h"

#ifdef USE_EDITOR

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui_internal.h"
#include "utilsModule/imguiHelper/imgui_neo_sequencer.h"
#include "editorRender.h"
#include "IconsFontAwesome5.h"
#include "imgui.h"
#include "renderModule/backends/gl/materialGl.h"
#include "resourceModule/materialManager.h"

#include "sceneModule/sceneManager.h"
#include "misc/cpp/imgui_stdlib.h"
#include "resourceModule/textureManager.h"
#include "utilsModule/imguiHelper/imguiWidgets.h"
#include "utilsModule/time/time.h"

#include <utilsModule/imguiHelper/nodeEditor/imgui_node_editor.h>

enum class PinType {
    Flow,
    Bool,
    Int,
    Float,
    String,
    Object,
    Function,
    Delegate,
};

enum class PinKind {
    Output,
    Input
};



struct Pin {
    ax::NodeEditor::PinId   ID;
    std::string Name;
    PinType Type;
    PinKind Kind;
    ax::NodeEditor::NodeId NodeId;

    Pin(int id, const char* name, PinType type, ax::NodeEditor::NodeId NodeId, PinKind Kind) :
        ID(id), NodeId(NodeId), Name(name), Type(type), Kind(Kind) {
    }
};

struct Link {
    ax::NodeEditor::LinkId ID;

    ax::NodeEditor::PinId StartPinID;
    ax::NodeEditor::PinId EndPinID;

    ImColor Color;

    Link(ax::NodeEditor::LinkId id, ax::NodeEditor::PinId startPinId, ax::NodeEditor::PinId endPinId) :
        ID(id), StartPinID(startPinId), EndPinID(endPinId), Color(255, 255, 255) {
    }
};

template<class Enum, class Node>
struct NodeEditor {
    NodeEditor() {
        init();
    };
    NodeEditor(const std::string& configPath): configPath(configPath) {
        init();
    }

    int GetNextId() const {
        static int m_NextId = 1;
        return m_NextId++;
    }

    bool IsPinLinked(ax::NodeEditor::PinId id) const {
        if (!id)
            return false;

        for (auto& link : m_Links)
            if (link.StartPinID == id || link.EndPinID == id)
                return true;

        return false;
    }

    ax::NodeEditor::EditorContext* m_Editor = nullptr;
    ax::NodeEditor::Config config;
    std::string configPath = "nodeEditor.json";
    std::string widgetName = "Node Animation Editor";
    std::vector<Node> m_Nodes;
    std::vector<Link> m_Links;
    

    enum class IconType : ImU32 { Flow, Circle, Square, Grid, RoundSquare, Diamond };
    const int m_PinIconSize = 24;


    ImColor GetIconColor(PinType type) {
        switch (type) {
        default:
        case PinType::Flow:     return ImColor(255, 255, 255);
        case PinType::Bool:     return ImColor(220, 48, 48);
        case PinType::Int:      return ImColor(68, 201, 156);
        case PinType::Float:    return ImColor(147, 226, 74);
        case PinType::String:   return ImColor(124, 21, 153);
        case PinType::Object:   return ImColor(51, 150, 215);
        case PinType::Function: return ImColor(218, 0, 183);
        case PinType::Delegate: return ImColor(255, 48, 48);
        }
    };
    void DrawIcon(ImDrawList* drawList, const ImVec2& a, const ImVec2& b, IconType type, bool filled, ImU32 color, ImU32 innerColor) {
        auto rect = ImRect(a, b);
        auto rect_x = rect.Min.x;
        auto rect_y = rect.Min.y;
        auto rect_w = rect.Max.x - rect.Min.x;
        auto rect_h = rect.Max.y - rect.Min.y;
        auto rect_center_x = (rect.Min.x + rect.Max.x) * 0.5f;
        auto rect_center_y = (rect.Min.y + rect.Max.y) * 0.5f;
        auto rect_center = ImVec2(rect_center_x, rect_center_y);
        const auto outline_scale = rect_w / 24.0f;
        const auto extra_segments = static_cast<int>(2 * outline_scale); // for full circle

        if (type == IconType::Flow) {
            const auto origin_scale = rect_w / 24.0f;

            const auto offset_x = 1.0f * origin_scale;
            const auto offset_y = 0.0f * origin_scale;
            const auto margin = (filled ? 2.0f : 2.0f) * origin_scale;
            const auto rounding = 0.1f * origin_scale;
            const auto tip_round = 0.7f; // percentage of triangle edge (for tip)
            //const auto edge_round = 0.7f; // percentage of triangle edge (for corner)
            const auto canvas = ImRect(
                rect.Min.x + margin + offset_x,
                rect.Min.y + margin + offset_y,
                rect.Max.x - margin + offset_x,
                rect.Max.y - margin + offset_y);
            const auto canvas_x = canvas.Min.x;
            const auto canvas_y = canvas.Min.y;
            const auto canvas_w = canvas.Max.x - canvas.Min.x;
            const auto canvas_h = canvas.Max.y - canvas.Min.y;

            const auto left = canvas_x + canvas_w * 0.5f * 0.3f;
            const auto right = canvas_x + canvas_w - canvas_w * 0.5f * 0.3f;
            const auto top = canvas_y + canvas_h * 0.5f * 0.2f;
            const auto bottom = canvas_y + canvas_h - canvas_h * 0.5f * 0.2f;
            const auto center_y = (top + bottom) * 0.5f;
            //const auto angle = AX_PI * 0.5f * 0.5f * 0.5f;

            const auto tip_top = ImVec2(canvas_x + canvas_w * 0.5f, top);
            const auto tip_right = ImVec2(right, center_y);
            const auto tip_bottom = ImVec2(canvas_x + canvas_w * 0.5f, bottom);

            drawList->PathLineTo(ImVec2(left, top) + ImVec2(0, rounding));
            drawList->PathBezierCubicCurveTo(
                ImVec2(left, top),
                ImVec2(left, top),
                ImVec2(left, top) + ImVec2(rounding, 0));
            drawList->PathLineTo(tip_top);
            drawList->PathLineTo(tip_top + (tip_right - tip_top) * tip_round);
            drawList->PathBezierCubicCurveTo(
                tip_right,
                tip_right,
                tip_bottom + (tip_right - tip_bottom) * tip_round);
            drawList->PathLineTo(tip_bottom);
            drawList->PathLineTo(ImVec2(left, bottom) + ImVec2(rounding, 0));
            drawList->PathBezierCubicCurveTo(
                ImVec2(left, bottom),
                ImVec2(left, bottom),
                ImVec2(left, bottom) - ImVec2(0, rounding));

            if (!filled) {
                if (innerColor & 0xFF000000)
                    drawList->AddConvexPolyFilled(drawList->_Path.Data, drawList->_Path.Size, innerColor);

                drawList->PathStroke(color, true, 2.0f * outline_scale);
            } else
                drawList->PathFillConvex(color);
        } else {
            auto triangleStart = rect_center_x + 0.32f * rect_w;

            auto rect_offset = -static_cast<int>(rect_w * 0.25f * 0.25f);

            rect.Min.x += rect_offset;
            rect.Max.x += rect_offset;
            rect_x += rect_offset;
            rect_center_x += rect_offset * 0.5f;
            rect_center.x += rect_offset * 0.5f;

            if (type == IconType::Circle) {
                const auto c = rect_center;

                if (!filled) {
                    const auto r = 0.5f * rect_w / 2.0f - 0.5f;

                    if (innerColor & 0xFF000000)
                        drawList->AddCircleFilled(c, r, innerColor, 12 + extra_segments);
                    drawList->AddCircle(c, r, color, 12 + extra_segments, 2.0f * outline_scale);
                } else {
                    drawList->AddCircleFilled(c, 0.5f * rect_w / 2.0f, color, 12 + extra_segments);
                }
            }

            if (type == IconType::Square) {
                if (filled) {
                    const auto r = 0.5f * rect_w / 2.0f;
                    const auto p0 = rect_center - ImVec2(r, r);
                    const auto p1 = rect_center + ImVec2(r, r);

#if IMGUI_VERSION_NUM > 18101
                    drawList->AddRectFilled(p0, p1, color, 0, ImDrawFlags_RoundCornersAll);
#else
                    drawList->AddRectFilled(p0, p1, color, 0, 15);
#endif
                } else {
                    const auto r = 0.5f * rect_w / 2.0f - 0.5f;
                    const auto p0 = rect_center - ImVec2(r, r);
                    const auto p1 = rect_center + ImVec2(r, r);

                    if (innerColor & 0xFF000000) {
#if IMGUI_VERSION_NUM > 18101
                        drawList->AddRectFilled(p0, p1, innerColor, 0, ImDrawFlags_RoundCornersAll);
#else
                        drawList->AddRectFilled(p0, p1, innerColor, 0, 15);
#endif
                    }

#if IMGUI_VERSION_NUM > 18101
                    drawList->AddRect(p0, p1, color, 0, ImDrawFlags_RoundCornersAll, 2.0f * outline_scale);
#else
                    drawList->AddRect(p0, p1, color, 0, 15, 2.0f * outline_scale);
#endif
                }
            }

            if (type == IconType::Grid) {
                const auto r = 0.5f * rect_w / 2.0f;
                const auto w = ceilf(r / 3.0f);

                const auto baseTl = ImVec2(floorf(rect_center_x - w * 2.5f), floorf(rect_center_y - w * 2.5f));
                const auto baseBr = ImVec2(floorf(baseTl.x + w), floorf(baseTl.y + w));

                auto tl = baseTl;
                auto br = baseBr;
                for (int i = 0; i < 3; ++i) {
                    tl.x = baseTl.x;
                    br.x = baseBr.x;
                    drawList->AddRectFilled(tl, br, color);
                    tl.x += w * 2;
                    br.x += w * 2;
                    if (i != 1 || filled)
                        drawList->AddRectFilled(tl, br, color);
                    tl.x += w * 2;
                    br.x += w * 2;
                    drawList->AddRectFilled(tl, br, color);

                    tl.y += w * 2;
                    br.y += w * 2;
                }

                triangleStart = br.x + w + 1.0f / 24.0f * rect_w;
            }

            if (type == IconType::RoundSquare) {
                if (filled) {
                    const auto r = 0.5f * rect_w / 2.0f;
                    const auto cr = r * 0.5f;
                    const auto p0 = rect_center - ImVec2(r, r);
                    const auto p1 = rect_center + ImVec2(r, r);

#if IMGUI_VERSION_NUM > 18101
                    drawList->AddRectFilled(p0, p1, color, cr, ImDrawFlags_RoundCornersAll);
#else
                    drawList->AddRectFilled(p0, p1, color, cr, 15);
#endif
                } else {
                    const auto r = 0.5f * rect_w / 2.0f - 0.5f;
                    const auto cr = r * 0.5f;
                    const auto p0 = rect_center - ImVec2(r, r);
                    const auto p1 = rect_center + ImVec2(r, r);

                    if (innerColor & 0xFF000000) {
#if IMGUI_VERSION_NUM > 18101
                        drawList->AddRectFilled(p0, p1, innerColor, cr, ImDrawFlags_RoundCornersAll);
#else
                        drawList->AddRectFilled(p0, p1, innerColor, cr, 15);
#endif
                    }

#if IMGUI_VERSION_NUM > 18101
                    drawList->AddRect(p0, p1, color, cr, ImDrawFlags_RoundCornersAll, 2.0f * outline_scale);
#else
                    drawList->AddRect(p0, p1, color, cr, 15, 2.0f * outline_scale);
#endif
                }
            } else if (type == IconType::Diamond) {
                if (filled) {
                    const auto r = 0.607f * rect_w / 2.0f;
                    const auto c = rect_center;

                    drawList->PathLineTo(c + ImVec2(0, -r));
                    drawList->PathLineTo(c + ImVec2(r, 0));
                    drawList->PathLineTo(c + ImVec2(0, r));
                    drawList->PathLineTo(c + ImVec2(-r, 0));
                    drawList->PathFillConvex(color);
                } else {
                    const auto r = 0.607f * rect_w / 2.0f - 0.5f;
                    const auto c = rect_center;

                    drawList->PathLineTo(c + ImVec2(0, -r));
                    drawList->PathLineTo(c + ImVec2(r, 0));
                    drawList->PathLineTo(c + ImVec2(0, r));
                    drawList->PathLineTo(c + ImVec2(-r, 0));

                    if (innerColor & 0xFF000000)
                        drawList->AddConvexPolyFilled(drawList->_Path.Data, drawList->_Path.Size, innerColor);

                    drawList->PathStroke(color, true, 2.0f * outline_scale);
                }
            } else {
                const auto triangleTip = triangleStart + rect_w * (0.45f - 0.32f);

                drawList->AddTriangleFilled(
                    ImVec2(ceilf(triangleTip), rect_y + rect_h * 0.5f),
                    ImVec2(triangleStart, rect_center_y + 0.15f * rect_h),
                    ImVec2(triangleStart, rect_center_y - 0.15f * rect_h),
                    color);
            }
        }
    }
    void Icon(const ImVec2& size, IconType type, bool filled, const ImVec4& color/* = ImVec4(1, 1, 1, 1)*/, const ImVec4& innerColor/* = ImVec4(0, 0, 0, 0)*/) {
        if (ImGui::IsRectVisible(size)) {
            auto cursorPos = ImGui::GetCursorScreenPos();
            auto drawList = ImGui::GetWindowDrawList();
            DrawIcon(drawList, cursorPos, cursorPos + size, type, filled, ImColor(color), ImColor(innerColor));
        }

        ImGui::Dummy(size);
    }
    void DrawPinIcon(const Pin& pin, bool connected, int alpha) {
        IconType iconType;
        ImColor  color = GetIconColor(pin.Type);
        color.Value.w = alpha / 255.0f;
        switch (pin.Type) {
        case PinType::Flow:     iconType = IconType::Flow;   break;
        case PinType::Bool:     iconType = IconType::Circle; break;
        case PinType::Int:      iconType = IconType::Circle; break;
        case PinType::Float:    iconType = IconType::Circle; break;
        case PinType::String:   iconType = IconType::Circle; break;
        case PinType::Object:   iconType = IconType::Circle; break;
        case PinType::Function: iconType = IconType::Circle; break;
        case PinType::Delegate: iconType = IconType::Square; break;
        default:
            return;
        }

        Icon(ImVec2(static_cast<float>(m_PinIconSize), static_cast<float>(m_PinIconSize)), iconType, connected, color, ImColor(32, 32, 32, alpha));
    };

    void init() {
        config.SettingsFile = configPath.c_str();
        //config.UserPointer = this;
        m_Editor = ax::NodeEditor::CreateEditor(&config);
    }


    virtual void drawOutput(Pin& output) {
        //for (auto& output : node.Outputs) {
            auto alpha = ImGui::GetStyle().Alpha;
            //if (newLinkPin && !CanCreateLink(newLinkPin, &output) && &output != newLinkPin)
            //    alpha = alpha * (48.0f / 255.0f);

            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            ax::NodeEditor::BeginPin(output.ID, ax::NodeEditor::PinKind::Output);
            if (output.Type == PinType::String) {
                //static char buffer[128] = "Edit Me\nMultiline!";
                //static bool wasActive = false;
                //
                //ImGui::PushItemWidth(100.0f);
                //ImGui::InputText("##edit", buffer, 127);
                //ImGui::PopItemWidth();
                //if (ImGui::IsItemActive() && !wasActive) {
                //    ed::EnableShortcuts(false);
                //    wasActive = true;
                //} else if (!ImGui::IsItemActive() && wasActive) {
                //    ed::EnableShortcuts(true);
                //    wasActive = false;
                //}
            }
            if (!output.Name.empty()) {
                ImGui::TextUnformatted(output.Name.c_str());
            }
            DrawPinIcon(output, IsPinLinked(output.ID), (int)(alpha * 255));
            ImGui::PopStyleVar();
            ax::NodeEditor::EndPin();
        //}
    }

    virtual void drawInput(Pin& input) {
        //for (auto& input : node.Inputs) {
            auto alpha = ImGui::GetStyle().Alpha;

            ax::NodeEditor::BeginPin(input.ID, ax::NodeEditor::PinKind::Input);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
            DrawPinIcon(input, IsPinLinked(input.ID), (int)(alpha * 255));
            if (!input.Name.empty()) {
                ImGui::TextUnformatted(input.Name.c_str());
            }
            if (input.Type == PinType::Bool) {
                ImGui::Button("Hello");
            }
            ImGui::PopStyleVar();
            ax::NodeEditor::EndPin();
        //}
    }

    virtual void drawContent(Node& node) {
	    
    }

    virtual void drawNode(Node& node) {
        ax::NodeEditor::BeginNode(node.ID);
        ImGui::Text(node.Name.c_str());

        const int pinWidth = 40;
        const int contentWidth = 100;
        //const int nodeWidth = contentWidth + (node.Inputs.empty() ? 0 : pinWidth) + (node.Outputs.empty() ? 0 : pinWidth);

        ImGui::Dummy({contentWidth, 0});

        const int mxSz = std::max(node.Inputs.size(), node.Outputs.size());
        for (int i = 0; i < mxSz; ++i) {
            if (i < node.Inputs.size()) {
                ImGui::PushItemWidth(pinWidth);
                drawInput(node.Inputs[i]);
                ImGui::PopItemWidth();
            } else {
                ImGui::Dummy({pinWidth, 0});
            }
            
            ImGui::SameLine(contentWidth);
            if (i < node.Outputs.size()) {
                ImGui::PushItemWidth(pinWidth);
                drawOutput(node.Outputs[i]);
                ImGui::PopItemWidth();
            } else {
                ImGui::Dummy({pinWidth, 0});
            }

        }

        ImGui::PushItemWidth(100);
        drawContent(node);
        ImGui::PopItemWidth();

        ax::NodeEditor::EndNode();
    }

    virtual void drawNodes() {
        for (auto& node : m_Nodes) {
            drawNode(node);
        }
    }

    virtual void drawLinks() {
        for (auto& link : m_Links) {
            ax::NodeEditor::Link(link.ID, link.StartPinID, link.EndPinID, link.Color, 2.0f);
        }
    }

    Pin* findPin(ax::NodeEditor::PinId id) {
        if (!id)
            return nullptr;

        for (auto& node : m_Nodes) {
            for (auto& pin : node.Inputs)
                if (pin.ID == id)
                    return &pin;

            for (auto& pin : node.Outputs)
                if (pin.ID == id)
                    return &pin;
        }
        return nullptr;
    }

    Link* findLinkByPinId(ax::NodeEditor::PinId pin) {
	    for (auto& link : m_Links) {
		    if (link.StartPinID == pin || link.EndPinID == pin) {
                return &link;
		    }
	    }
        return nullptr;
    }

    Node* findNodeByPinId(ax::NodeEditor::PinId id) {
        for (auto& node : m_Nodes) {
            for (auto& pin : node.Inputs) {
                if (pin.ID == id) {
                    return &node;
                }
            }

            for (auto& pin : node.Outputs) {
                if (pin.ID == id) {
                    return &node;
                }
            }
        }
        return nullptr;
    }

    Node* findNodeById(ax::NodeEditor::NodeId id) {
        for (auto& node : m_Nodes) {
            if (node.ID == id) {
                return &node;
            }
        }
        return nullptr;
    }

    virtual void createAction() {
        if (ax::NodeEditor::BeginCreate(ImColor(255, 255, 255), 2.0f)) {
            ax::NodeEditor::PinId startPinId = 0, endPinId = 0;
            if (ax::NodeEditor::QueryNewLink(&startPinId, &endPinId)) {
                if (startPinId != endPinId && !findLinkByPinId(startPinId) && !findLinkByPinId(endPinId)) {
                    auto startPin = findPin(startPinId);
                    auto endPin = findPin(endPinId);
                    if (startPin && endPin && ax::NodeEditor::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
                        bool check = true;

                        if (startPin->Kind == endPin->Kind) {
                            check = false;
                        }
                        if (startPin->Type != endPin->Type) {
                            check = false;
                        }
                        if (startPin->NodeId == endPin->NodeId) {
                            check = false;
                        }

                        if (startPin->Kind == PinKind::Input) {
                            std::swap(startPinId, endPinId);
                        }

                        if (check) {
                            m_Links.emplace_back(GetNextId(), startPinId, endPinId);
                        }
                        else {
                            ax::NodeEditor::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                        }
                    }
                }
            }

            ax::NodeEditor::PinId pinId = 0;
            if (ax::NodeEditor::QueryNewNode(&pinId)) {
                
            }
        }
        ax::NodeEditor::EndCreate();
    }

    virtual void deleteAction() {
        if (ax::NodeEditor::BeginDelete()) {
            ax::NodeEditor::NodeId nodeId = 0;
            while (ax::NodeEditor::QueryDeletedNode(&nodeId)) {
                if (ax::NodeEditor::AcceptDeletedItem()) {
                    auto id = std::find_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](auto& node) { return node.ID == nodeId; });
                    if (id != m_Nodes.end()) {
                        for (auto& pin : id->Inputs) {
                            auto link = findLinkByPinId(pin.ID);
                            if (link) {
                                std::erase_if(m_Links, [id = link->ID](Link& link) { return link.ID == id; });
                            }
                        }
                        for (auto& pin : id->Outputs) {
                            auto link = findLinkByPinId(pin.ID);
                            if (link) {
                                std::erase_if(m_Links, [id = link->ID](Link& link) { return link.ID == id; });
                            }
                        }
                        m_Nodes.erase(id);
                    }
                }
            }

            ax::NodeEditor::LinkId linkId = 0;
            while (ax::NodeEditor::QueryDeletedLink(&linkId)) {
                if (ax::NodeEditor::AcceptDeletedItem()) {
                    auto id = std::find_if(m_Links.begin(), m_Links.end(), [linkId](auto& link) { return link.ID == linkId; });
                    if (id != m_Links.end()) {
                        m_Links.erase(id);
                    }
                }
            }
        }
        ax::NodeEditor::EndDelete();
    }

    virtual void renderGraphEditor() {
        ax::NodeEditor::SetCurrentEditor(m_Editor);
        ax::NodeEditor::Begin(widgetName.c_str(), ImVec2(0.0, 0.0f));

        drawNodes();
        drawLinks();

        createAction();
        deleteAction();

        ax::NodeEditor::End();
        ax::NodeEditor::SetCurrentEditor(nullptr);
    }
};

enum class NodeType {
    Start,
    Wait,
    Position,
    Scale,
    Rotation,
    Sequence,
    Simultaneous,
    Event
};

struct Node {
    ax::NodeEditor::NodeId ID;
    std::string Name;
    std::vector<Pin> Inputs;
    std::vector<Pin> Outputs;
    ImColor Color;
    NodeType Type;
    ImVec2 Size;

    IKIGAI::ANIMATION::PropType value;
    float time = 0.0f;
    int objId = 0;
    IKIGAI::ANIMATION::InterpolationType curveType = IKIGAI::ANIMATION::InterpolationType::CUSTOM;
    IKIGAI::ANIMATION::Curve curve;

    Node(int id, const char* name, NodeType Type, ImColor color = ImColor(255, 255, 255)) :
        ID(id), Name(name), Color(color), Type(Type), Size(0, 0) {
    }
};

struct AnimationNodeEditor: public NodeEditor<NodeType, Node> {
    AnimationNodeEditor():NodeEditor<NodeType, Node>("nodeEditor.json") {
        addStartNode();
        addWaitNode();
        addSequenceNode();
        addSimultaneousNode();
    }

    void addStartNode() {
        m_Nodes.emplace_back(GetNextId(), "Start Node", NodeType::Start, ImColor(255, 128, 128));
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Start", PinType::Flow, m_Nodes.back().ID, PinKind::Output);
    }

    void addWaitNode() {
        m_Nodes.emplace_back(GetNextId(), "Wait", NodeType::Wait, ImColor(255, 128, 128));
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "In", PinType::Flow, m_Nodes.back().ID, PinKind::Input);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Flow, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Object, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().value = 0.0f;
    }

    void addSimultaneousNode() {
        m_Nodes.emplace_back(GetNextId(), "Simultaneous", NodeType::Simultaneous, ImColor(255, 128, 128));
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "In", PinType::Flow, m_Nodes.back().ID, PinKind::Input);
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "In", PinType::Object, m_Nodes.back().ID, PinKind::Input);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Flow, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Object, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().value = 0.0f;
    }

    void addSequenceNode() {
        m_Nodes.emplace_back(GetNextId(), "Sequence", NodeType::Sequence, ImColor(255, 128, 128));
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "In", PinType::Flow, m_Nodes.back().ID, PinKind::Input);
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "In", PinType::Object, m_Nodes.back().ID, PinKind::Input);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Flow, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Object, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().value = 0.0f;
    }

    void addEventNode() {
        m_Nodes.emplace_back(GetNextId(), "Event", NodeType::Event, ImColor(255, 128, 128));
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "In", PinType::Flow, m_Nodes.back().ID, PinKind::Input);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Flow, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Object, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().value = std::string();
    }

    void addPosNode() {
        m_Nodes.emplace_back(GetNextId(), "Pos", NodeType::Position, ImColor(255, 128, 128));
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "In", PinType::Flow, m_Nodes.back().ID, PinKind::Input);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Flow, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Object, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().value = IKIGAI::MATH::Vector3f();
    }

    void addRotateNode() {
        m_Nodes.emplace_back(GetNextId(), "Rotate", NodeType::Rotation, ImColor(255, 128, 128));
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "In", PinType::Flow, m_Nodes.back().ID, PinKind::Input);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Flow, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Object, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().value = IKIGAI::MATH::Vector3f();
    }

    void addScaleNode() {
        m_Nodes.emplace_back(GetNextId(), "Scale", NodeType::Scale, ImColor(255, 128, 128));
        m_Nodes.back().Inputs.emplace_back(GetNextId(), "In", PinType::Flow, m_Nodes.back().ID, PinKind::Input);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Flow, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().Outputs.emplace_back(GetNextId(), "Out", PinType::Object, m_Nodes.back().ID, PinKind::Output);
        m_Nodes.back().value = IKIGAI::MATH::Vector3f();
    }

    void createNode(NodeType type) {
	    switch (type) {
        case NodeType::Start: addStartNode();  break;
        case NodeType::Wait: addWaitNode(); break;
        case NodeType::Position: addPosNode(); break;
        case NodeType::Scale: addScaleNode(); break;
        case NodeType::Rotation: addRotateNode(); break;
        case NodeType::Sequence: addSequenceNode(); break;
        case NodeType::Simultaneous:  addSimultaneousNode(); break;
        default: break;
	    }
    }

    void drawContent(Node& node) override {
        ImGui::PushID(node.ID.Get());
        ImGui::InputInt("Object", &node.objId);
        ImGui::InputFloat("Time", &node.time);
        switch(node.Type) {
        case NodeType::Start: break;
        case NodeType::Wait: {
            //auto& val = std::get<float>(node.value);
            //ImGui::DragFloat("##wait", &val);
        }
        break;
        case NodeType::Scale:
        case NodeType::Position:
        case NodeType::Rotation: {
            auto& val = std::get<IKIGAI::MATH::Vector3f>(node.value);
            std::array<float, 3> data = {val.x, val.y, val.z};
            if (ImGui::DragFloat3("##wait", data.data())) {
                val.x = data[0];
                val.y = data[1];
                val.z = data[2];
            }
        }
        break;
        case NodeType::Sequence:break;
        case NodeType::Simultaneous: break;
        case NodeType::Event: {
            auto& val = std::get<std::string>(node.value);
            ImGui::InputText("##event", &val);
        }
        break;
        default: break;
        }

        auto& curve = node.curve;
        float v[5] = {curve.mLeftTangent.x, curve.mLeftTangent.y, curve.mRightTangent.x, curve.mRightTangent.y};
        if (ImGui::Bezier("Curve", v)) {
            curve.mLeftTangent = {v[0], v[1]};
            curve.mRightTangent = {v[2], v[3]};
        }

        ImGui::PopID();
    }

    virtual void createAction() override {
        if (ax::NodeEditor::BeginCreate(ImColor(255, 255, 255), 2.0f)) {
            ax::NodeEditor::PinId startPinId = 0, endPinId = 0;
            if (ax::NodeEditor::QueryNewLink(&startPinId, &endPinId)) {
                if (startPinId != endPinId && !findLinkByPinId(startPinId) && !findLinkByPinId(endPinId)) {
                    auto startPin = findPin(startPinId);
                    auto endPin = findPin(endPinId);
                    if (startPin && endPin && ax::NodeEditor::AcceptNewItem(ImColor(128, 255, 128), 4.0f)) {
                        bool check = true;

                        if (startPin->Kind == endPin->Kind) {
                            check = false;
                        }
                        if (startPin->Type != endPin->Type) {
                            check = false;
                        }
                        if (startPin->NodeId == endPin->NodeId) {
                            check = false;
                        }

                        if (startPin->Kind == PinKind::Input) {
                            std::swap(startPinId, endPinId);
                            std::swap(startPin, endPin);
                        }

                        if (check) {
                            m_Links.emplace_back(GetNextId(), startPinId, endPinId);

                            auto node = findNodeByPinId(endPinId);
                            if (endPin->Type == PinType::Object && node && (node->Type == NodeType::Simultaneous || node->Type == NodeType::Sequence)) {
                                node->Inputs.emplace_back(GetNextId(), "In", PinType::Object, m_Nodes.back().ID, PinKind::Input);
                            }


                        } else {
                            ax::NodeEditor::RejectNewItem(ImColor(255, 128, 128), 1.0f);
                        }
                    }
                }
            }

            ax::NodeEditor::PinId pinId = 0;
            if (ax::NodeEditor::QueryNewNode(&pinId)) {

            }
        }
        ax::NodeEditor::EndCreate();
    }


    virtual void deleteAction() override {
        if (ax::NodeEditor::BeginDelete()) {
            ax::NodeEditor::NodeId nodeId = 0;
            while (ax::NodeEditor::QueryDeletedNode(&nodeId)) {
                if (ax::NodeEditor::AcceptDeletedItem()) {
                    auto id = std::find_if(m_Nodes.begin(), m_Nodes.end(), [nodeId](auto& node) { return node.ID == nodeId; });
                    if (id != m_Nodes.end()) {
                        for (auto& pin : id->Inputs) {
                            auto link = findLinkByPinId(pin.ID);
                            if (link) {
                                std::erase_if(m_Links, [id = link->ID](Link& link) { return link.ID == id; });
                            }
                        }
                        for (auto& pin : id->Outputs) {
                            auto link = findLinkByPinId(pin.ID);
                            if (link) {
                                std::erase_if(m_Links, [id = link->ID](Link& link) { return link.ID == id; });
                            }
                        }
                        m_Nodes.erase(id);
                    }
                }
            }

            ax::NodeEditor::LinkId linkId = 0;
            while (ax::NodeEditor::QueryDeletedLink(&linkId)) {
                if (ax::NodeEditor::AcceptDeletedItem()) {
                    auto id = std::find_if(m_Links.begin(), m_Links.end(), [linkId](auto& link) { return link.ID == linkId; });
                    if (id != m_Links.end()) {

                        auto node = findNodeByPinId(id->EndPinID);
                        auto endPin = findPin(id->EndPinID);
                        if (endPin->Type == PinType::Object && node && (node->Type == NodeType::Simultaneous || node->Type == NodeType::Sequence)) {
                            std::erase_if(node->Inputs, [id = id->EndPinID](Pin& pin) {
                                return id == pin.ID;
                            });
                        }

                        m_Links.erase(id);
                    }
                }
            }
        }
        ax::NodeEditor::EndDelete();
    }

    struct NodeAnimNodeDescriptor {
        int id;
        NodeType nodeType;
        IKIGAI::ANIMATION::PropType value;
        float time = 1.0f;
        int objId = 0;

        IKIGAI::ANIMATION::InterpolationType curveType = IKIGAI::ANIMATION::InterpolationType::CUSTOM;
        IKIGAI::ANIMATION::Curve curve;
        std::vector<NodeAnimNodeDescriptor> childs;

        template<class Context>
        constexpr static auto serde(Context& context, NodeAnimNodeDescriptor& value) {
            using Self = NodeAnimNodeDescriptor;
            using namespace serde::attribute;
            serde::serde_struct(context, value)
                .field(&Self::id, "Id")
                .field(&Self::nodeType, "NodeType")
                .field(&Self::value, "Value")
                .field(&Self::time, "Time")
                .field(&Self::curveType, "CurveType")
                .field(&Self::curve, "Curve")
                .field(&Self::childs, "Childs")
                .field(&Self::objId, "ObjId");
        }
    };
    struct NodeAnimDescriptor {
        std::vector<NodeAnimNodeDescriptor> nodes;

        template<class Context>
        constexpr static auto serde(Context& context, NodeAnimDescriptor& value) {
            using Self = NodeAnimDescriptor;
            using namespace serde::attribute;
            serde::serde_struct(context, value)
                .field(&Self::nodes, "Nodes");
        }
    };


    void createNodeDescriptor(Node* node, NodeAnimNodeDescriptor& desc) {
        desc.nodeType = node->Type;
	    switch (node->Type)
	    {
	    case NodeType::Wait:
        {
            desc.time = node->time;
            break;
        }
	    case NodeType::Position:
	    case NodeType::Scale:
	    case NodeType::Rotation:
        {
            desc.objId = node->objId;
            desc.time = node->time;
            desc.value = node->value;
            desc.curve = node->curve;
            desc.curveType = node->curveType;
            break;
        };
	    case NodeType::Event:
        {
            desc.time = node->time;
            desc.value = node->value;
            break;
        }
        case NodeType::Simultaneous:
        case NodeType::Sequence: {
            //Skip first it is flow
            for (int i = 1; i < node->Inputs.size(); ++i) {
                auto link = findLinkByPinId(node->Inputs[i].ID);
                if (link) {
                    auto _node = findNodeByPinId(link->StartPinID);
                    if (_node) {
                        NodeAnimNodeDescriptor nodeDesc;
                        createNodeDescriptor(_node, nodeDesc);
                        desc.childs.push_back(nodeDesc);
                    }
                }
            }
        }
	    break;
        default: break;
	    }
    }

    void createChilds(ax::NodeEditor::NodeId nodeId, NodeAnimNodeDescriptor& childDesc) {
        createNode(childDesc.nodeType);
        m_Nodes.back().time = childDesc.time;
        m_Nodes.back().objId = childDesc.objId;
        m_Nodes.back().value = childDesc.value;
        m_Nodes.back().curve = childDesc.curve;
        m_Nodes.back().curveType = childDesc.curveType;
        
        auto node = findNodeById(nodeId); //to
        auto child = &m_Nodes.back();//from

        auto startPinId = child->Outputs[1].ID;
        auto endPinId = node->Inputs[1].ID;
        m_Links.emplace_back(GetNextId(), startPinId, endPinId);

        if (!childDesc.childs.empty()) {
            auto childId = child->ID;
            for (auto& ch : childDesc.childs) {
                createChilds(childId, ch);
            }
        }
    }

    ax::NodeEditor::NodeId createFlow(ax::NodeEditor::NodeId prevNodeId, NodeAnimNodeDescriptor& desc) {
        createNode(desc.nodeType);
        m_Nodes.back().time = desc.time;
        m_Nodes.back().objId = desc.objId;
        m_Nodes.back().value = desc.value;
        m_Nodes.back().curve = desc.curve;
        m_Nodes.back().curveType = desc.curveType;

        auto prevNode = findNodeById(prevNodeId);
        auto node = &m_Nodes.back();
        auto nodeId = node->ID;

        auto startPinId = prevNode->Outputs[0].ID;
        auto endPinId = node->Inputs[0].ID;
        m_Links.emplace_back(GetNextId(), startPinId, endPinId);

        if (!desc.childs.empty()) {
	        for (auto& ch : desc.childs) {
                createChilds(nodeId, ch);
	        }
        }

        return nodeId;
    }

    void createEditorFromDescriptor(NodeAnimDescriptor& desc) {
        m_Nodes.clear();
        m_Links.clear();
        createNode(NodeType::Start);


        auto prevNodeId = m_Nodes[0].ID;
        for (auto& d : desc.nodes) {
            prevNodeId = createFlow(prevNodeId, d);
        }
    }

    std::unique_ptr<IKIGAI::ANIMATION::NodableAnimationNode> createAnimationFlow(NodeAnimNodeDescriptor& desc) {
        std::unique_ptr<IKIGAI::ANIMATION::NodableAnimationNode> res;

        switch (desc.nodeType)
        {
        case NodeType::Start: {
        }break;
        case NodeType::Wait: {
            res = std::make_unique<IKIGAI::ANIMATION::NodableAnimationNodeWait>(desc.time);
        }break;
        case NodeType::Position: {
            res = std::make_unique<IKIGAI::ANIMATION::NodableAnimationNodePos>(IKIGAI::ECS::Object::Id(IKIGAI::ECS::Object::Id::ID(desc.objId)), std::get<IKIGAI::MATH::Vector3f>(desc.value), desc.time, desc.curveType);
            static_cast<IKIGAI::ANIMATION::NodableAnimationNodePos*>(res.get())->curve = desc.curve;
        }break;
        case NodeType::Scale: {
            res = std::make_unique<IKIGAI::ANIMATION::NodableAnimationNodeScale>(IKIGAI::ECS::Object::Id(IKIGAI::ECS::Object::Id::ID(desc.objId)), std::get<IKIGAI::MATH::Vector3f>(desc.value), desc.time, desc.curveType);
            static_cast<IKIGAI::ANIMATION::NodableAnimationNodeScale*>(res.get())->curve = desc.curve;
        }break;
        case NodeType::Rotation: {
            res = std::make_unique<IKIGAI::ANIMATION::NodableAnimationNodeRotate>(IKIGAI::ECS::Object::Id(IKIGAI::ECS::Object::Id::ID(desc.objId)), std::get<IKIGAI::MATH::Vector3f>(desc.value), desc.time, desc.curveType);
            static_cast<IKIGAI::ANIMATION::NodableAnimationNodeRotate*>(res.get())->curve = desc.curve;
        }break;
        case NodeType::Sequence:  {
            res = std::make_unique<IKIGAI::ANIMATION::NodableAnimationNodeSequence>();
            for (auto& ch : desc.childs) {
                static_cast<IKIGAI::ANIMATION::NodableAnimationNodeSequence*>(res.get())->add(createAnimationFlow(ch));
            }
        }break;
        case NodeType::Simultaneous: {
            res = std::make_unique<IKIGAI::ANIMATION::NodableAnimationNodeSimultaneous>();
            for (auto& ch : desc.childs) {
                static_cast<IKIGAI::ANIMATION::NodableAnimationNodeSimultaneous*>(res.get())->add(createAnimationFlow(ch));
            }
        }break;
        case NodeType::Event: {
            res = std::make_unique<IKIGAI::ANIMATION::NodableAnimationNodeEvent>(std::get<std::string>(desc.value), desc.time);
        }break;
        default: break;
        }

        return std::move(res);
    }

    void createAnimationFromDescriptor(NodeAnimDescriptor& desc) {
        std::unique_ptr<IKIGAI::ANIMATION::NodableAnimation> animation = std::make_unique<IKIGAI::ANIMATION::NodableAnimation>();
        for (auto& d : desc.nodes) {
            animation->animations.push_back(createAnimationFlow(d));
        }
    }

    NodeAnimDescriptor createAnimationDescriptor() {
        auto findNodeWithType = [this](NodeType type) {
            std::vector<Node*> nodes;
            for (auto& n : m_Nodes) {
	            if (n.Type == type) {
                    nodes.push_back(&n);
	            }
            }
            return nodes;
        };

        NodeAnimDescriptor desc;

        auto startNode = findNodeWithType(NodeType::Start).at(0);

        auto link = findLinkByPinId(startNode->Outputs[0].ID);
        while (link != nullptr) {
            auto node = findNodeByPinId(link->EndPinID);

            NodeAnimNodeDescriptor nodeDesc;
            createNodeDescriptor(node, nodeDesc);
            desc.nodes.push_back(nodeDesc);

            link = findLinkByPinId(node->Outputs[0].ID);
        }
        return desc;
    }
};





void IKIGAI::EDITOR::StatWindow::draw() {
	static std::vector<float> values(100, 0.0f);
	static int valuesOffset = 0;
	static float updateTime = 0;
	static float timeToUpdate = 0;
	static double fps = 0.0;
	static double dt;

	ImGui::Begin("Stats", nullptr,
        ImGuiWindowFlags_NoScrollWithMouse |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
	//auto& timer = TIME::Timer::GetInstance();
	//if (timeToUpdate <= 0.0) {
	//	fps = timer.getFPS();
	//	dt = timer.getDeltaTimeUnscaled().count();
	//	values[valuesOffset] = fps;
	//	valuesOffset = (valuesOffset + 1) % values.size();
	//	timeToUpdate = updateTime;
	//} else {
	//	timeToUpdate -= timer.getDeltaTimeUnscaled().count();
	//}
	//if (IMGUI::SliderFloatWithSteps("Update time", &updateTime, 0.0f, 1.0f, 0.10f)) {
	//	timeToUpdate = updateTime;
	//}
	//ImGui::Text("FPS: %f", fps);
	//ImGui::Text("Delta: %f", dt);
	//ImGui::PlotLines("##FPSGraph", values.data(), values.size(), valuesOffset, nullptr, -10.0f, 500.0f, ImVec2(0, 80.0f));

    static std::unique_ptr<AnimationNodeEditor> editor = std::make_unique<AnimationNodeEditor>();

    if (ImGui::Button("Save")) {
        ImGui::OpenPopup("Save new node animation");
    }
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        editor->m_Nodes.clear();
        editor->m_Links.clear();
        editor->createNode(NodeType::Start);
    }

    if (ImGui::BeginTable("File Browser Table", 2, ImGuiTableFlags_Reorderable | ImGuiTableFlags_Resizable)) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        
        ImGui::BeginChild("##fileTree_child_win");

        if (ImGui::Button("Wait")) {
            editor->createNode(NodeType::Wait);
        }
        if (ImGui::Button("Sequence")) {
            editor->createNode(NodeType::Sequence);
        }
        if (ImGui::Button("Simultaneous")) {
            editor->createNode(NodeType::Simultaneous);
        }
        if (ImGui::Button("Position")) {
            editor->createNode(NodeType::Position);
        }
        if (ImGui::Button("Scale")) {
            editor->createNode(NodeType::Scale);
        }
        if (ImGui::Button("Rotation")) {
            editor->createNode(NodeType::Rotation);
        }

        ImGui::EndChild();

        ImGui::TableNextColumn();

        editor->renderGraphEditor();

        ImGui::EndTable();
    }


    if (ImGui::BeginPopupModal("Save new node animation", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        static std::string name = "newAnimation";

        ImGui::Text("Enter name");
        ImGui::SameLine();
        ImGui::InputText("#animName", &name);

        ImGui::Separator();

        if (ImGui::Button("OK", ImVec2(120, 0))) {
            if (!name.empty()) {
                auto desc = editor->createAnimationDescriptor();
                auto res = UTILS::ToJsonStr(desc);

                if (res.isErr()) {
                    //LOG_ERROR << "Can not save: " << sceneFilePath;
                }

                auto jsonStr = res.unwrap();
                //write
                //auto path = UTILS::GetRealPath("animations/") + name + ".trackanim";
                //std::ofstream f(path);
                //if (f.is_open()) {
                //    f << jsonStr << std::endl;
                //}
                //f.close();

                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }



	ImGui::End();
}



#endif
