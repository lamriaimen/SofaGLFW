﻿/******************************************************************************
*                 SOFA, Simulation Open-Framework Architecture                *
*                    (c) 2006 INRIA, USTL, UJF, CNRS, MGH                     *
*                                                                             *
* This program is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU General Public License as published by the Free  *
* Software Foundation; either version 2 of the License, or (at your option)   *
* any later version.                                                          *
*                                                                             *
* This program is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for    *
* more details.                                                               *
*                                                                             *
* You should have received a copy of the GNU General Public License along     *
* with this program. If not, see <http://www.gnu.org/licenses/>.              *
*******************************************************************************
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#include <iomanip>
#include <ostream>
#include <SofaGLFW/SofaGLFWImgui.h>
#include <SofaGLFW/SofaGLFWBaseGUI.h>

#include <SofaGLFW/config.h>

#include <sofa/core/CategoryLibrary.h>
#include <sofa/helper/logging/LoggingMessageHandler.h>

#include <sofa/core/loader/SceneLoader.h>
#include <sofa/simulation/SceneLoaderFactory.h>

#include <sofa/helper/system/FileSystem.h>
#include <sofa/simulation/Simulation.h>

#if SOFAGLFW_HAS_IMGUI
#include <imgui.h>
#include <imgui_internal.h> //imgui_internal.h is included in order to use the DockspaceBuilder API (which is still in development)
#include <ImGuiFileDialog.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include <sofa/helper/Utils.h>
#include <sofa/type/vector.h>
#include <sofa/simulation/Node.h>
#include <SofaBaseVisual/VisualStyle.h>
#include <sofa/core/ComponentLibrary.h>
#include <sofa/core/ObjectFactory.h>
#include <sofa/helper/system/PluginManager.h>
#endif

namespace sofa::glfw::imgui
{

void imguiInit()
{
#if SOFAGLFW_HAS_IMGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    static const std::string iniFile(helper::Utils::getExecutableDirectory() + "/imgui.ini");
    io.IniFilename = iniFile.c_str();

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
#endif
}

void imguiInitBackend(GLFWwindow* glfwWindow)
{
#if SOFAGLFW_HAS_IMGUI
    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(glfwWindow, true);
    ImGui_ImplOpenGL3_Init(nullptr);
#endif
}

void imguiDraw(SofaGLFWBaseGUI* baseGUI)
{
    auto groot = baseGUI->getRootNode();
#if SOFAGLFW_HAS_IMGUI
    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;

    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    window_flags |= ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace", nullptr, window_flags);
    ImGui::PopStyleVar();
    ImGui::PopStyleVar(2);

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode);

    static auto first_time = true;
    if (first_time)
    {
        first_time = false;

        ImGui::DockBuilderRemoveNode(dockspace_id); // clear any previous layout
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

        auto dock_id_right = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.4f, nullptr, &dockspace_id);
        ImGui::DockBuilderDockWindow("Scene Graph", dock_id_right);
        auto dock_id_down = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.3f, nullptr, &dockspace_id);
        ImGui::DockBuilderDockWindow("Log", dock_id_down);
        auto dock_id_left = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Left, 0.2f, nullptr, &dockspace_id);
        ImGui::DockBuilderDockWindow("Controls", dock_id_left);
        ImGui::DockBuilderFinish(dockspace_id);
    }
    ImGui::End();


    const ImGuiIO& io = ImGui::GetIO();

    static bool isControlsWindowOpen = true;
    static bool isPerformancesWindowOpen = false;
    static bool isSceneGraphWindowOpen = true;
    static bool isDisplayFlagsWindowOpen = false;
    static bool isPluginsWindowOpen = false;
    static bool isComponentsWindowOpen = false;
    static bool isLogWindowOpen = true;

    static bool showFPSInMenuBar = true;

    ImVec2 mainMenuBarSize;

    /***************************************
     * Main menu bar
     **************************************/
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open Simulation"))
            {
                std::string filter;
                std::string allTypesSpace, allTypesComa;
                simulation::SceneLoaderFactory::SceneLoaderList* loaders =simulation::SceneLoaderFactory::getInstance()->getEntries();
                for (auto it=loaders->begin(); it!=loaders->end(); ++it)
                {
                    filter += (*it)->getFileTypeDesc();
                    filter += " (";
                    sofa::simulation::SceneLoader::ExtensionList extensions;
                    (*it)->getExtensionList(&extensions);
                    for (auto itExt=extensions.begin(); itExt!=extensions.end(); ++itExt)
                    {
                        filter += "*." + *itExt;
                        allTypesSpace += "*." + *itExt;
                        if (itExt != extensions.end() - 1)
                        {
                            filter += " ";
                            allTypesSpace += " ";
                        }
                    }
                    filter+=")";
                    filter+="{";
                    for (auto itExt=extensions.begin(); itExt!=extensions.end(); ++itExt)
                    {
                        filter += "." + *itExt;
                        allTypesComa += "." + *itExt;
                        if (itExt != extensions.end() - 1)
                        {
                            filter += ",";
                            allTypesComa += ",";
                        }
                    }
                    filter+="}";
                    if (it!=loaders->end() - 1)
                    {
                        filter+=",";
                    }

                }

                filter = "All (" + allTypesSpace + "){" + allTypesComa + "}," + filter + ",.*";

                ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", filter.c_str(), baseGUI->getFilename());
            }
            if (ImGui::MenuItem("Close Simulation"))
            {
                sofa::simulation::getSimulation()->unload(groot);
                baseGUI->setSimulationIsRunning(false);
                sofa::simulation::getSimulation()->init(baseGUI->getRootNode().get());
                return;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit"))
            {
                //TODO: brutal exit, need to clean up everything (simulation, window, opengl, imgui etc)
                exit(EXIT_SUCCESS);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::Checkbox("Show FPS", &showFPSInMenuBar);
            bool isFullScreen = baseGUI->isFullScreen();
            if (ImGui::Checkbox("Fullscreen", &isFullScreen))
            {
                baseGUI->switchFullScreen();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Center Camera"))
            {
                sofa::component::visualmodel::BaseCamera::SPtr camera;
                groot->get(camera);
                if (camera)
                {
                    if( groot->f_bbox.getValue().isValid() && !groot->f_bbox.getValue().isFlat() )
                    {
                        camera->fitBoundingBox(groot->f_bbox.getValue().minBBox(), groot->f_bbox.getValue().maxBBox());
                    }
                }
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows"))
        {
            ImGui::Checkbox("Controls", &isControlsWindowOpen);
            ImGui::Checkbox("Performances", &isPerformancesWindowOpen);
            ImGui::Checkbox("Scene Graph", &isSceneGraphWindowOpen);
            ImGui::Checkbox("Display Flags", &isDisplayFlagsWindowOpen);
            ImGui::Checkbox("Plugins", &isPluginsWindowOpen);
            ImGui::Checkbox("Components", &isComponentsWindowOpen);
            ImGui::Checkbox("Log", &isLogWindowOpen);
            ImGui::EndMenu();
        }
        if (showFPSInMenuBar)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize(std::to_string(io.Framerate).c_str()).x
                - ImGui::GetScrollX() - 2 * ImGui::GetStyle().ItemSpacing.x);
            ImGui::Text("%.1f FPS", io.Framerate);
        }
        mainMenuBarSize = ImGui::GetWindowSize();
        ImGui::EndMainMenuBar();
    }

    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey"))
    {
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            const std::string filePathName = ImGuiFileDialog::Instance()->GetFilePathName();

            if (helper::system::FileSystem::exists(filePathName))
            {
                sofa::simulation::getSimulation()->unload(groot);

                groot = sofa::simulation::getSimulation()->load(filePathName.c_str());
                if( !groot )
                    groot = sofa::simulation::getSimulation()->createNewGraph("");
                baseGUI->setSimulation(groot, filePathName);

                sofa::simulation::getSimulation()->init(groot.get());
                auto camera = baseGUI->findCamera(groot);
                if (camera)
                {
                    camera->fitBoundingBox(groot->f_bbox.getValue().minBBox(), groot->f_bbox.getValue().maxBBox());
                    baseGUI->changeCamera(camera);
                }
                baseGUI->initVisual();
            }
        }
        ImGuiFileDialog::Instance()->Close();
        return;
    }

    /***************************************
     * Controls window
     **************************************/
    if (isControlsWindowOpen)
    {
        ImGui::SetNextWindowPos(ImVec2(0, mainMenuBarSize.y), ImGuiCond_FirstUseEver);
        if (ImGui::Begin("Controls", &isControlsWindowOpen))
        {
            auto& animate = sofa::helper::getWriteAccessor(groot->animate_).wref();
            ImGui::Checkbox("Animate", &animate);

            if (ImGui::Button("Reset"))
            {
                groot->setTime(0.);
                simulation::getSimulation()->reset ( groot.get() );
            }
        }
        ImGui::End();
    }

    /***************************************
     * Performances window
     **************************************/
    if (isPerformancesWindowOpen)
    {
        static sofa::type::vector<float> msArray;
        if (ImGui::Begin("Performances", &isPerformancesWindowOpen))
        {
            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::Text("%d vertices, %d indices (%d triangles)", io.MetricsRenderVertices, io.MetricsRenderIndices, io.MetricsRenderIndices / 3);
            ImGui::Text("%d visible windows, %d active allocations", io.MetricsRenderWindows, io.MetricsActiveAllocations);

            msArray.push_back(1000.0f / io.Framerate);
            if (msArray.size() >= 2000)
            {
                msArray.erase(msArray.begin());
            }
            ImGui::PlotLines("Frame Times", msArray.data(), msArray.size(), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(0, 100));
        }
        ImGui::End();
    }

    /***************************************
     * Scene graph window
     **************************************/
    if (isSceneGraphWindowOpen)
    {
        if (ImGui::Begin("Scene Graph", &isSceneGraphWindowOpen))
        {
            unsigned int treeDepth {};
            static core::objectmodel::Base* clickedObject { nullptr };

            std::function<void(simulation::Node*)> showNode;
            showNode = [&showNode, &treeDepth](simulation::Node* node)
            {
                if (node == nullptr) return;
                if (treeDepth == 0)
                    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                const bool open = ImGui::TreeNode(node->getName().c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("Node");
                if (ImGui::IsItemClicked())
                    clickedObject = node;
                if (open)
                {
                    ++treeDepth;
                    for (const auto child : node->getChildren())
                    {
                        showNode(dynamic_cast<simulation::Node*>(child));
                    }

                    for (const auto object : node->getNodeObjects())
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TreeNodeEx(object->getName().c_str(), ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
                        if (ImGui::IsItemClicked())
                            clickedObject = object;
                        ImGui::TableNextColumn();
                        ImGui::Text(object->getClassName().c_str());
                    }

                    --treeDepth;
                    ImGui::TreePop();
                }
            };

            static ImGuiTableFlags flags = ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody;
            if (ImGui::BeginTable("sceneGraphTable", 2, flags))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_NoHide);
                ImGui::TableSetupColumn("Class Name", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("A").x * 12.0f);
                ImGui::TableHeadersRow();

                showNode(groot.get());

                ImGui::EndTable();
            }

            static bool areDataDisplayed;
            areDataDisplayed = clickedObject != nullptr;
            if (clickedObject != nullptr)
            {
                ImGui::Separator();
                ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
                if (ImGui::CollapsingHeader(clickedObject->getName().c_str(), &areDataDisplayed))
                {
                    ImGui::Indent();
                    std::map<std::string, std::vector<const core::BaseData*> > groupMap;
                    for (const auto* data : clickedObject->getDataFields())
                    {
                        groupMap[data->getGroup()].push_back(data);
                    }
                    for (const auto [group, datas] : groupMap)
                    {
                        const auto groupName = group.empty() ? "Property" : group;
                        ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
                        if (ImGui::CollapsingHeader(groupName.c_str()))
                        {
                            ImGui::Indent();
                            for (const auto& data : datas)
                            {
                                const bool isOpen = ImGui::CollapsingHeader(data->m_name.c_str());
                                if (ImGui::IsItemHovered())
                                {
                                    ImGui::BeginTooltip();
                                    ImGui::TextDisabled(data->getHelp().c_str());
                                    ImGui::EndTooltip();
                                }
                                if (isOpen)
                                {
                                    ImGui::TextDisabled(data->getHelp().c_str());
                                    ImGui::TextWrapped(data->getValueString().c_str());
                                }
                            }
                            ImGui::Unindent();
                        }
                    }
                    ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
                    if (ImGui::CollapsingHeader("Links"))
                    {
                        ImGui::Indent();
                        for (const auto* link : clickedObject->getLinks())
                        {
                            const auto linkValue = link->getValueString();
                            const auto linkTitle = link->getName();

                            const bool isOpen = ImGui::CollapsingHeader(linkTitle.c_str());
                            if (ImGui::IsItemHovered())
                            {
                                ImGui::BeginTooltip();
                                ImGui::TextDisabled(link->getHelp().c_str());
                                ImGui::EndTooltip();
                            }
                            if (isOpen)
                            {
                                ImGui::TextDisabled(link->getHelp().c_str());
                                ImGui::TextWrapped(linkValue.c_str());
                            }
                        }
                        ImGui::Unindent();
                    }
                    ImGui::Unindent();
                }
                if (!areDataDisplayed)
                {
                    clickedObject = nullptr;
                }
            }
        }
        ImGui::End();
    }

    /***************************************
     * Display flags window
     **************************************/
    if (isDisplayFlagsWindowOpen)
    {
        if (ImGui::Begin("Display Flags", &isDisplayFlagsWindowOpen))
        {

            component::visualmodel::VisualStyle::SPtr visualStyle = nullptr;
            groot->get(visualStyle);
            if (visualStyle)
            {
                auto& displayFlags = sofa::helper::getWriteAccessor(visualStyle->displayFlags).wref();

                {
                    const bool initialValue = displayFlags.getShowVisualModels();
                    bool changeableValue = initialValue;
                    ImGui::Checkbox("Show Visual Models", &changeableValue);
                    if (changeableValue != initialValue)
                    {
                        displayFlags.setShowVisualModels(changeableValue);
                    }
                }

                {
                    const bool initialValue = displayFlags.getShowBehaviorModels();
                    bool changeableValue = initialValue;
                    ImGui::Checkbox("Show Behavior Models", &changeableValue);
                    if (changeableValue != initialValue)
                    {
                        displayFlags.setShowBehaviorModels(changeableValue);
                    }
                }

                {
                    const bool initialValue = displayFlags.getShowForceFields();
                    bool changeableValue = initialValue;
                    ImGui::Checkbox("Show Force Fields", &changeableValue);
                    if (changeableValue != initialValue)
                    {
                        displayFlags.setShowForceFields(changeableValue);
                    }
                }

                {
                    const bool initialValue = displayFlags.getShowCollisionModels();
                    bool changeableValue = initialValue;
                    ImGui::Checkbox("Show Collision Models", &changeableValue);
                    if (changeableValue != initialValue)
                    {
                        displayFlags.setShowCollisionModels(changeableValue);
                    }
                }

                {
                    const bool initialValue = displayFlags.getShowBoundingCollisionModels();
                    bool changeableValue = initialValue;
                    ImGui::Checkbox("Show Bounding Collision Models", &changeableValue);
                    if (changeableValue != initialValue)
                    {
                        displayFlags.setShowBoundingCollisionModels(changeableValue);
                    }
                }

                {
                    const bool initialValue = displayFlags.getShowMappings();
                    bool changeableValue = initialValue;
                    ImGui::Checkbox("Show Mappings", &changeableValue);
                    if (changeableValue != initialValue)
                    {
                        displayFlags.setShowMappings(changeableValue);
                    }
                }

                {
                    const bool initialValue = displayFlags.getShowMechanicalMappings();
                    bool changeableValue = initialValue;
                    ImGui::Checkbox("Show Mechanical Mappings", &changeableValue);
                    if (changeableValue != initialValue)
                    {
                        displayFlags.setShowMechanicalMappings(changeableValue);
                    }
                }

                {
                    const bool initialValue = displayFlags.getShowWireFrame();
                    bool changeableValue = initialValue;
                    ImGui::Checkbox("Show Wire Frame", &changeableValue);
                    if (changeableValue != initialValue)
                    {
                        displayFlags.setShowWireFrame(changeableValue);
                    }
                }

                {
                    const bool initialValue = displayFlags.getShowNormals();
                    bool changeableValue = initialValue;
                    ImGui::Checkbox("Show Normals", &changeableValue);
                    if (changeableValue != initialValue)
                    {
                        displayFlags.setShowNormals(changeableValue);
                    }
                }

            }
        }
        ImGui::End();
    }

    /***************************************
     * Plugins window
     **************************************/
    if (isPluginsWindowOpen)
    {
        if (ImGui::Begin("Plugins", &isPluginsWindowOpen))
        {
            ImGui::Columns(2);

            const auto& pluginMap = helper::system::PluginManager::getInstance().getPluginMap();

            static std::map<std::string, bool> isSelected;
            static std::string selectedPlugin;
            for (const auto& [path, plugin] : pluginMap)
            {
                if (ImGui::Selectable(plugin.getModuleName(), selectedPlugin == path))
                {
                    selectedPlugin = path;
                }
            }

            ImGui::NextColumn();

            const auto pluginIt = pluginMap.find(selectedPlugin);
            if (pluginIt != pluginMap.end())
            {
                ImGui::Text("Plugin: %s", pluginIt->second.getModuleName());
                ImGui::Text("Version: %s", pluginIt->second.getModuleVersion());
                ImGui::Text("License: %s", pluginIt->second.getModuleLicense());
                ImGui::Spacing();
                ImGui::TextDisabled("Description:");
                ImGui::TextWrapped("%s", pluginIt->second.getModuleDescription());
                ImGui::Spacing();
                ImGui::TextDisabled("Components:");
                ImGui::TextWrapped("%s", pluginIt->second.getModuleComponentList());
                ImGui::Spacing();
                ImGui::TextDisabled("Path:");
                ImGui::TextWrapped(selectedPlugin.c_str());
            }
        }
        ImGui::End();
    }

    /***************************************
     * Components window
     **************************************/
    if (isComponentsWindowOpen)
    {
        if (ImGui::Begin("Components", &isComponentsWindowOpen))
        {
            unsigned int nbLoadedComponents = 0;
            if (ImGui::BeginTable("split", 2, ImGuiTableFlags_BordersOuter | ImGuiTableFlags_Resizable))
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();

                static core::ClassEntry::SPtr selectedEntry;

                static std::vector<core::ClassEntry::SPtr> entries;
                entries.clear();
                core::ObjectFactory::getInstance()->getAllEntries(entries);
                nbLoadedComponents = entries.size();

                static ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersV | ImGuiTableFlags_BordersOuterH | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_NoBordersInBody | ImGuiTableFlags_Sortable;
                if (ImGui::BeginTable("componentTable", 2, flags, ImVec2(0.f, 400.f)))
                {
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Category");
                    ImGui::TableHeadersRow();

                    struct ComponentEntry
                    {
                        std::string name;
                        std::string category;
                        core::ClassEntry::SPtr classEntry;
                    };
                    static std::vector<ComponentEntry> componentEntries;
                    if (componentEntries.empty())
                    {
                        for (const auto& entry : entries)
                        {
                            std::set<std::string> categoriesSet;
                            std::stringstream templateSs;
                            for (const auto& [templateInstance, creator] : entry->creatorMap)
                            {
                                std::vector<std::string> categories;
                                core::CategoryLibrary::getCategories(entry->creatorMap.begin()->second->getClass(), categories);
                                categoriesSet.insert(categories.begin(), categories.end());
                            }
                            std::stringstream categorySs;
                            for (const auto& c : categoriesSet)
                                categorySs << c << ", ";

                            const std::string categoriesText = categorySs.str().substr(0, categorySs.str().size()-2);

                            componentEntries.push_back({entry->className, categoriesText, entry});
                        }
                    }

                    if (ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs())
                    {
                        if (sorts_specs->SpecsDirty)
                        {
                            std::sort(componentEntries.begin(), componentEntries.end(), [sorts_specs](const ComponentEntry& lhs, const ComponentEntry& rhs)
                            {
                                for (int n = 0; n < sorts_specs->SpecsCount; n++)
                                {
                                    const ImGuiTableColumnSortSpecs* sort_spec = &sorts_specs->Specs[n];
                                    const bool ascending = sort_spec->SortDirection == ImGuiSortDirection_Ascending;
                                    switch (sort_spec->ColumnIndex)
                                    {
                                    case 0:
                                        {
                                            if (lhs.name < rhs.name) return ascending;
                                            if (lhs.name > rhs.name) return !ascending;
                                            break;
                                        }
                                    case 1:
                                        {
                                            if (lhs.category < rhs.category) return ascending;
                                            if (lhs.category > rhs.category) return !ascending;
                                            break;
                                            return lhs.name < rhs.name;
                                        }
                                    default:
                                        IM_ASSERT(0); break;
                                    }
                                }
                                return false;
                            });
                        }
                    }

                    static const std::map<std::string, ImVec4> colorMap = []()
                    {
                        std::map<std::string, ImVec4> m;
                        int i {};
                        auto categories = core::CategoryLibrary::getCategories();
                        std::sort(categories.begin(), categories.end(), std::less<std::string>());
                        for (const auto& cat : categories)
                        {
                            ImVec4 color;
                            color.w = 1.f;
                            ImGui::ColorConvertHSVtoRGB(i++ / (static_cast<float>(categories.size())-1.f), 0.72f, 1.f, color.x, color.y, color.z);
                            m[cat] = color;
                        }
                        return m;
                    }();

                    for (const auto& entry : componentEntries)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        if (ImGui::Selectable(entry.name.c_str(), entry.classEntry == selectedEntry))
                            selectedEntry = entry.classEntry;
                        ImGui::TableNextColumn();

                        const auto colorIt = colorMap.find(entry.category);
                        if (colorIt != colorMap.end())
                            ImGui::TextColored(colorIt->second, colorIt->first.c_str());
                        else
                            ImGui::Text(entry.category.c_str());
                    }
                    ImGui::EndTable();
                }

                ImGui::TableNextColumn();

                if (selectedEntry)
                {
                    ImGui::Text("Name: %s", selectedEntry->className.c_str());
                    ImGui::Spacing();
                    ImGui::TextDisabled("Description:");
                    ImGui::TextWrapped(selectedEntry->description.c_str());
                    ImGui::Spacing();

                    bool hasTemplate = false;
                    for (const auto& [templateInstance, creator] : selectedEntry->creatorMap)
                    {
                        if (hasTemplate |= !templateInstance.empty())
                            break;
                    }

                    if (hasTemplate)
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Templates:");
                        for (const auto& [templateInstance, creator] : selectedEntry->creatorMap)
                        {
                            ImGui::BulletText(templateInstance.c_str());
                        }
                    }

                    if (!selectedEntry->aliases.empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Aliases:");
                        for (const auto& alias : selectedEntry->aliases)
                        {
                            ImGui::BulletText(alias.c_str());
                        }
                    }

                    std::set<std::string> namespaces;
                    for (const auto& [templateInstance, creator] : selectedEntry->creatorMap)
                    {
                        namespaces.insert(creator->getClass()->namespaceName);
                    }
                    if (!namespaces.empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Namespaces:");
                        for (const auto& nm : namespaces)
                        {
                            ImGui::BulletText(nm.c_str());
                        }
                    }

                    std::set<std::string> parents;
                    for (const auto& [templateInstance, creator] : selectedEntry->creatorMap)
                    {
                        for (const auto& p : creator->getClass()->parents)
                        {
                            parents.insert(p->className);
                        }
                    }
                    if (!parents.empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Parents:");
                        for (const auto& p : parents)
                        {
                            ImGui::BulletText(p.c_str());
                        }
                    }

                    std::set<std::string> targets;
                    for (const auto& [templateInstance, creator] : selectedEntry->creatorMap)
                    {
                        targets.insert(creator->getTarget());
                    }
                    if (!targets.empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Targets:");
                        for (const auto& t : targets)
                        {
                            ImGui::BulletText(t.c_str());
                        }
                    }
                }
                else
                {
                    ImGui::Text("Select a component");
                }

                ImGui::EndTable();
            }
            ImGui::Text("%d loaded components", nbLoadedComponents);
        }
        ImGui::End();
    }

    /***************************************
     * Log window
     **************************************/
    if (isLogWindowOpen)
    {
        if (ImGui::Begin("Log", &isLogWindowOpen))
        {
            unsigned int i {};
            const auto& messages = sofa::helper::logging::MainLoggingMessageHandler::getInstance().getMessages();
            const int digits = [&messages]()
            {
                int d = 0;
                auto s = messages.size();
                while (s != 0) { s /= 10; d++; }
                return d;
            }();

            if (ImGui::BeginTable("logTable", 4, ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("logId", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("message type", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("sender", ImGuiTableColumnFlags_WidthFixed);
                ImGui::TableSetupColumn("message", ImGuiTableColumnFlags_WidthStretch);
                for (const auto& message : messages)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    std::stringstream ss;
                    ss << std::setfill('0') << std::setw(digits) << i++;
                    ImGui::Text(ss.str().c_str());

                    ImGui::TableNextColumn();

                    constexpr auto writeMessageType = [](const helper::logging::Message::Type t)
                    {
                        switch (t)
                        {
                        case helper::logging::Message::Advice     : return ImGui::TextColored(ImVec4(0.f, 0.5686f, 0.9176f, 1.f), "[SUGGESTION]");
                        case helper::logging::Message::Deprecated : return ImGui::TextColored(ImVec4(0.5529f, 0.4314f, 0.3882f, 1.f), "[DEPRECATED]");
                        case helper::logging::Message::Warning    : return ImGui::TextColored(ImVec4(1.f, 0.4275f, 0.f, 1.f), "[WARNING]");
                        case helper::logging::Message::Info       : return ImGui::Text("[INFO]");
                        case helper::logging::Message::Error      : return ImGui::TextColored(ImVec4(0.8667f, 0.1725f, 0.f, 1.f), "[ERROR]");
                        case helper::logging::Message::Fatal      : return ImGui::TextColored(ImVec4(0.8353, 0.f, 0.f, 1.f), "[FATAL]");
                        case helper::logging::Message::TEmpty     : return ImGui::Text("[EMPTY]");
                        default: return;
                        }
                    };
                    writeMessageType(message.type());

                    ImGui::TableNextColumn();
                    ImGui::Text(message.sender().c_str());

                    ImGui::TableNextColumn();
                    ImGui::TextWrapped(message.message().str().c_str());
                }
                ImGui::EndTable();
            }
        }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Update and Render additional Platform Windows
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
#endif
}

void imguiTerminate()
{
#if SOFAGLFW_HAS_IMGUI
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
#endif
}

bool dispatchMouseEvents()
{
#if SOFAGLFW_HAS_IMGUI
    return !ImGui::GetIO().WantCaptureMouse;
#else
    return true;
#endif

}
} //namespace sofa::glfw::imgui