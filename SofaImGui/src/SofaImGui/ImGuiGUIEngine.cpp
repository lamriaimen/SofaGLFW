/******************************************************************************
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
#include <SofaImGui/ImGuiGUIEngine.h>

#include <iomanip>
#include <ostream>
#include <unordered_set>
#include <SofaGLFW/SofaGLFWBaseGUI.h>

#include <sofa/core/CategoryLibrary.h>
#include <sofa/helper/logging/LoggingMessageHandler.h>

#include <sofa/core/loader/SceneLoader.h>
#include <sofa/simulation/SceneLoaderFactory.h>

#include <sofa/helper/system/FileSystem.h>
#include <sofa/simulation/Simulation.h>

#include <sofa/helper/AdvancedTimer.h>

#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_internal.h> //imgui_internal.h is included in order to use the DockspaceBuilder API (which is still in development)
#include <implot.h>
#include <nfd.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_opengl2.h>
#include <IconsFontAwesome5.h>
#include <fa-regular-400.h>
#include <fa-solid-900.h>
#include <filesystem>
#include <fstream>
#include <Roboto-Medium.h>
#include <Style.h>
#include <SofaImGui/ImGuiDataWidget.h>

#include <sofa/helper/Utils.h>
#include <sofa/type/vector.h>
#include <sofa/simulation/Node.h>
#include <sofa/component/visual/VisualStyle.h>
#include <sofa/core/ComponentLibrary.h>
#include <sofa/core/ObjectFactory.h>
#include <sofa/helper/system/PluginManager.h>
#include <SofaImGui/ObjectColor.h>
#include <sofa/core/visual/VisualParams.h>
#include <sofa/helper/io/File.h>
#include <sofa/component/visual/VisualGrid.h>
#include <sofa/component/visual/LineAxis.h>
#include <sofa/gl/component/rendering3d/OglSceneFrame.h>
#include <sofa/gui/common/BaseGUI.h>
#include <sofa/helper/io/STBImage.h>
#include <sofa/simulation/graph/DAGNode.h>
#include "windows/Performances.h"
#include "windows/Log.h"
#include "windows/Profiler.h"
#include "windows/SceneGraph.h"
#include "windows/DisplayFlags.h"


using namespace sofa;

namespace sofaimgui
{

constexpr const char* VIEW_FILE_EXTENSION = ".view";

void ImGuiGUIEngine::init()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    NFD_Init();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    static const std::string imguiIniFile(sofa::helper::Utils::getExecutableDirectory() + "/imgui.ini");
    io.IniFilename = imguiIniFile.c_str();

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;


    ini.SetUnicode();
    if (sofa::helper::system::FileSystem::exists(getAppIniFile()))
    {
        SI_Error rc = ini.LoadFile(getAppIniFile().c_str());
        assert(rc == SI_OK);
    }

    const char* pv;
    pv = ini.GetValue("Style", "theme");
    if (!pv)
    {
        ini.SetValue("Style", "theme", sofaimgui::defaultStyle.c_str(), "# Preset of colors and properties to change the theme of the application");
        SI_Error rc = ini.SaveFile(getAppIniFile().c_str());
        pv = sofaimgui::defaultStyle.c_str();
    }

    // Setup Dear ImGui style
    sofaimgui::setStyle(pv);

    sofa::helper::system::PluginManager::getInstance().readFromIniFile(
        sofa::gui::common::BaseGUI::getConfigDirectoryPath() + "/loadedPlugins.ini");
}

void ImGuiGUIEngine::initBackend(GLFWwindow* glfwWindow)
{
    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(glfwWindow, true);

#if SOFAIMGUI_FORCE_OPENGL2 == 1
    ImGui_ImplOpenGL2_Init();
#else
    ImGui_ImplOpenGL3_Init(nullptr);
#endif // SOFAIMGUI_FORCE_OPENGL2 == 1

    GLFWmonitor* monitor = glfwGetWindowMonitor(glfwWindow);
    if (!monitor)
    {
        monitor = glfwGetPrimaryMonitor();
    }
    if (monitor)
    {
        float xscale, yscale;
        glfwGetMonitorContentScale(monitor, &xscale, &yscale);

        ImGuiIO& io = ImGui::GetIO();

        io.Fonts->AddFontFromMemoryCompressedTTF(ROBOTO_MEDIUM_compressed_data, ROBOTO_MEDIUM_compressed_size, 16 * yscale);

        ImFontConfig config;
        config.MergeMode = true;
        config.GlyphMinAdvanceX = 16.0f; // Use if you want to make the icon monospaced
        static const ImWchar icon_ranges[] = { ICON_MIN_FA, ICON_MAX_FA, 0 };
        io.Fonts->AddFontFromMemoryCompressedTTF(FA_REGULAR_400_compressed_data, FA_REGULAR_400_compressed_size, 16 * yscale, &config, icon_ranges);
        io.Fonts->AddFontFromMemoryCompressedTTF(FA_SOLID_900_compressed_data, FA_SOLID_900_compressed_size, 16 * yscale, &config, icon_ranges);
    }
}

void ImGuiGUIEngine::loadFile(sofaglfw::SofaGLFWBaseGUI* baseGUI, sofa::core::sptr<sofa::simulation::Node>& groot, const std::string filePathName)
{
    sofa::simulation::node::unload(groot);

    groot = sofa::simulation::node::load(filePathName.c_str());
    if( !groot )
        groot = sofa::simulation::getSimulation()->createNewGraph("");
    baseGUI->setSimulation(groot, filePathName);

    sofa::simulation::node::initRoot(groot.get());
    auto camera = baseGUI->findCamera(groot);
    if (camera)
    {
        camera->fitBoundingBox(groot->f_bbox.getValue().minBBox(), groot->f_bbox.getValue().maxBBox());
        baseGUI->changeCamera(camera);
    }
    baseGUI->initVisual();
}

void ImGuiGUIEngine::showViewport(sofa::core::sptr<sofa::simulation::Node> groot, const char* const& windowNameViewport, bool& isViewportWindowOpen)
{
    if (isViewportWindowOpen)
    {
        ImVec2 pos;
        if (ImGui::Begin(windowNameViewport, &isViewportWindowOpen/*, ImGuiWindowFlags_MenuBar*/))
        {
            pos = ImGui::GetWindowPos();

            ImGui::BeginChild("Render");
            ImVec2 wsize = ImGui::GetWindowSize();
            m_viewportWindowSize = { wsize.x, wsize.y};

            ImGui::Image((ImTextureID)m_fbo->getColorTexture(), wsize, ImVec2(0, 1), ImVec2(1, 0));

            isMouseOnViewport = ImGui::IsItemHovered();
            ImGui::EndChild();

        }
        ImGui::End();

        if (isViewportWindowOpen && ini.GetBoolValue("Visualization", "showViewportSettingsButton", true))
        {
            static constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
            pos.x += 10;
            pos.y += 40;
            ImGui::SetNextWindowPos(pos);

            if (ImGui::Begin("viewportSettingsMenuWindow", &isViewportWindowOpen, window_flags))
            {
                if (ImGui::Button(ICON_FA_COG))
                {
                    ImGui::OpenPopup("viewportSettingsMenu");
                }

                if (ImGui::BeginPopup("viewportSettingsMenu"))
                {
                    if (ImGui::Selectable(ICON_FA_BORDER_ALL "  Show Grid"))
                    {
                        auto grid = groot->get<sofa::component::visual::VisualGrid>();
                        if (!grid)
                        {
                            auto newGrid = sofa::core::objectmodel::New<sofa::component::visual::VisualGrid>();
                            groot->addObject(newGrid);
                            newGrid->setName("viewportGrid");
                            newGrid->addTag(core::objectmodel::Tag("createdByGUI"));
                            newGrid->d_enable.setValue(true);
                            auto box = groot->f_bbox.getValue().maxBBox() - groot->f_bbox.getValue().minBBox();
                            newGrid->d_size.setValue(*std::max_element(box.begin(), box.end()));
                            newGrid->init();
                        }
                        else
                        {
                            grid->d_enable.setValue(!grid->d_enable.getValue());
                        }
                    }
                    if (ImGui::Selectable(ICON_FA_ARROWS_ALT "  Show Axis"))
                    {
                        auto axis = groot->get<sofa::component::visual::LineAxis>();
                        if (!axis)
                        {
                            auto newAxis = sofa::core::objectmodel::New<sofa::component::visual::LineAxis>();
                            groot->addObject(newAxis);
                            newAxis->setName("viewportAxis");
                            newAxis->addTag(core::objectmodel::Tag("createdByGUI"));
                            newAxis->d_enable.setValue(true);
                            auto box = groot->f_bbox.getValue().maxBBox() - groot->f_bbox.getValue().minBBox();
                            newAxis->d_size.setValue(*std::max_element(box.begin(), box.end()));
                            newAxis->init();
                        }
                        else
                        {
                            axis->d_enable.setValue(!axis->d_enable.getValue());
                        }
                    }
                    if (ImGui::Selectable(ICON_FA_SQUARE_FULL "  Show Frame"))
                    {
                        auto sceneFrame = groot->get<sofa::gl::component::rendering3d::OglSceneFrame>();
                        if (!sceneFrame)
                        {
                            auto newSceneFrame = sofa::core::objectmodel::New<sofa::gl::component::rendering3d::OglSceneFrame>();
                            groot->addObject(newSceneFrame);
                            newSceneFrame->setName("viewportFrame");
                            newSceneFrame->addTag(core::objectmodel::Tag("createdByGUI"));
                            newSceneFrame->d_drawFrame.setValue(true);
                            newSceneFrame->init();
                        }
                        else
                        {
                            sceneFrame->d_drawFrame.setValue(!sceneFrame->d_drawFrame.getValue());
                        }
                    }
                    ImGui::EndPopup();
                }
            }
            ImGui::End();
        }
    }
}

void ImGuiGUIEngine::showPlugins(const char* const& windowNamePlugins, bool& isPluginsWindowOpen)
{
    if (isPluginsWindowOpen)
    {
        if (ImGui::Begin(windowNamePlugins, &isPluginsWindowOpen))
        {
            if (ImGui::Button("Load"))
            {
                std::vector<nfdfilteritem_t> nfd_filters {
                    {"SOFA plugin", helper::system::DynamicLibrary::extension.c_str() } };

                nfdchar_t *outPath;
                nfdresult_t result = NFD_OpenDialog(&outPath, nfd_filters.data(), nfd_filters.size(), NULL);
                if (result == NFD_OKAY)
                {
                    if (helper::system::FileSystem::exists(outPath))
                    {
                        helper::system::PluginManager::getInstance().loadPluginByPath(outPath);
                        helper::system::PluginManager::getInstance().writeToIniFile(
                            sofa::gui::common::BaseGUI::getConfigDirectoryPath() + "/loadedPlugins.ini");
                    }
                }
            }

            ImGui::BeginChild("Plugins", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f, ImGui::GetContentRegionAvail().y), false, ImGuiWindowFlags_HorizontalScrollbar);

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

            ImGui::EndChild();
            ImGui::SameLine();

            if (!selectedPlugin.empty())
            {
                ImGui::BeginChild("selectedPlugin", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y), false, ImGuiWindowFlags_HorizontalScrollbar);

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

            ImGui::EndChild();
        }
        ImGui::End();
    }
}

void ImGuiGUIEngine::showComponents(const char* const& windowNameComponents, bool& isComponentsWindowOpen)
{
    if (isComponentsWindowOpen)
    {
        if (ImGui::Begin(windowNameComponents, &isComponentsWindowOpen))
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

                    ImGui::Separator();

                    struct DataInfo
                    {
                        sofa::type::vector<std::string> templateType;
                        std::string description;
                        std::string defaultValue;
                        std::string type;
                    };

                    std::map<std::string, std::map<std::string, DataInfo>> allData;
                    {
                        const auto tmpNode = core::objectmodel::New<simulation::graph::DAGNode>("tmp");
                        for (const auto& [templateInstance, creator] : selectedEntry->creatorMap)
                        {
                            core::objectmodel::BaseObjectDescription desc;
                            const auto object = creator->createInstance(tmpNode.get(), &desc);
                            if (object)
                            {
                                for (const auto& data : object->getDataFields())
                                {
                                    allData[data->getGroup()][data->getName()].templateType.push_back(templateInstance);
                                    allData[data->getGroup()][data->getName()].description = data->getHelp();
                                    allData[data->getGroup()][data->getName()].defaultValue = data->getDefaultValueString();
                                    allData[data->getGroup()][data->getName()].type = data->getValueTypeString();
                                }
                            }
                        }
                    }

                    if (!allData.empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextDisabled("Data:");

                        for (const auto& [group, templateData] : allData)
                        {
                            const auto groupName = group.empty() ? "Property" : group;
                            if (ImGui::CollapsingHeader(groupName.c_str()))
                            {
                                ImGui::Indent();
                                for (auto& data : templateData)
                                {
                                    if (ImGui::CollapsingHeader(data.first.c_str()))
                                    {
                                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                                        ImGui::TextWrapped(data.second.description.c_str());
                                        const std::string defaultValue = "default value: " + data.second.defaultValue;
                                        ImGui::TextWrapped(defaultValue.c_str());
                                        const std::string type = "type: " + data.second.type;
                                        ImGui::TextWrapped(type.c_str());

                                        ImGui::PopStyleColor();
                                    }
                                }
                                ImGui::Unindent();
                            }
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

            if (ImGui::Button(ICON_FA_SAVE" "))
            {
                nfdchar_t *outPath;
                const nfdresult_t result = NFD_SaveDialog(&outPath, nullptr, 0, nullptr, "log.txt");
                if (result == NFD_OKAY)
                {
                    static std::vector<core::ClassEntry::SPtr> entries;
                    entries.clear();
                    core::ObjectFactory::getInstance()->getAllEntries(entries);

                    if (!entries.empty())
                    {
                        std::ofstream outputFile;
                        outputFile.open(outPath, std::ios::out);



                        if (outputFile.is_open())
                        {
                            for (const auto& entry : entries)
                            {
                                struct EntryProperty
                                {
                                    std::set<std::string> categories;
                                    std::string target;
                                    bool operator<(const EntryProperty& other) const { return target < other.target && categories < other.categories; }
                                };
                                std::set<EntryProperty> entryProperties;

                                for (const auto& [templateInstance, creator] : entry->creatorMap)
                                {
                                    EntryProperty property;

                                    std::vector<std::string> categories;
                                    core::CategoryLibrary::getCategories(entry->creatorMap.begin()->second->getClass(), categories);
                                    property.categories.insert(categories.begin(), categories.end());
                                    property.target = creator->getTarget();

                                    entryProperties.insert(property);
                                }

                                for (const auto& [categories, target] : entryProperties)
                                {
                                    outputFile
                                            << entry->className << ','
                                            << sofa::helper::join(categories.begin(), categories.end(), ';') << ','
                                            << target << ','
                                            << '\n';
                                }
                            }

                            outputFile.close();
                        }
                    }


                }

            }
        }
        ImGui::End();
    }
}

void ImGuiGUIEngine::showSettings(const char* const& windowNameSettings, bool& isSettingsOpen)
{
    if (isSettingsOpen)
    {
        if (ImGui::Begin(windowNameSettings, &isSettingsOpen))
        {
            const char* theme = ini.GetValue("Style", "theme", sofaimgui::defaultStyle.c_str());
            static std::size_t styleCurrent = std::distance(std::begin(sofaimgui::listStyles), std::find_if(std::begin(sofaimgui::listStyles), std::end(sofaimgui::listStyles),
                [&theme](const char* el){return std::string(el) == std::string(theme); }));
            if (ImGui::BeginCombo("Theme",  sofaimgui::listStyles[styleCurrent]))
            {
                for (std::size_t n = 0 ; n < sofaimgui::listStyles.size(); ++n)
                {
                    const bool isSelected = styleCurrent == n;
                    if (ImGui::Selectable(sofaimgui::listStyles[n], isSelected))
                    {
                        styleCurrent = n;

                        sofaimgui::setStyle(sofaimgui::listStyles[styleCurrent]);
                        ini.SetValue("Style", "theme", sofaimgui::listStyles[styleCurrent], "Preset of colors and properties to change the theme of the application");
                        SI_Error rc = ini.SaveFile(getAppIniFile().c_str());
                    }
                    if (isSelected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }


            ImGuiIO& io = ImGui::GetIO();
            const float MIN_SCALE = 0.3f;
            const float MAX_SCALE = 2.0f;
            ImGui::DragFloat("global scale", &io.FontGlobalScale, 0.005f, MIN_SCALE, MAX_SCALE, "%.2f", ImGuiSliderFlags_AlwaysClamp); // Scale everything


            bool alwaysShowFrame = ini.GetBoolValue("Visualization", "alwaysShowFrame", true);
            if (ImGui::Checkbox("Always show scene frame", &alwaysShowFrame))
            {
                ini.SetBoolValue("Visualization", "alwaysShowFrame", alwaysShowFrame);
                SI_Error rc = ini.SaveFile(getAppIniFile().c_str());
            }

            bool showViewportSettingsButton = ini.GetBoolValue("Visualization", "showViewportSettingsButton", true);
            if (ImGui::Checkbox("Show viewport settings button", &showViewportSettingsButton))
            {
                ini.SetBoolValue("Visualization", "showViewportSettingsButton", showViewportSettingsButton);
                SI_Error rc = ini.SaveFile(getAppIniFile().c_str());
            }

        }
        ImGui::End();
    }
}

const std::string& ImGuiGUIEngine::getAppIniFile()
{
    static const std::string appIniFile(sofa::helper::Utils::getExecutableDirectory() + "/settings.ini");
    return appIniFile;
}

void ImGuiGUIEngine::startFrame(sofaglfw::SofaGLFWBaseGUI* baseGUI)
{
    auto groot = baseGUI->getRootNode();

    bool alwaysShowFrame = ini.GetBoolValue("Visualization", "alwaysShowFrame", true);
    if (alwaysShowFrame)
    {
        auto sceneFrame = groot->get<sofa::gl::component::rendering3d::OglSceneFrame>();
        if (!sceneFrame)
        {
            auto newSceneFrame = sofa::core::objectmodel::New<sofa::gl::component::rendering3d::OglSceneFrame>();
            groot->addObject(newSceneFrame);
            newSceneFrame->setName("viewportFrame");
            newSceneFrame->addTag(core::objectmodel::Tag("createdByGUI"));
            newSceneFrame->d_drawFrame.setValue(true);
            newSceneFrame->init();
        }
    }

    // Start the Dear ImGui frame
#if SOFAIMGUI_FORCE_OPENGL2 == 1
    ImGui_ImplOpenGL2_NewFrame();
#else
    ImGui_ImplOpenGL3_NewFrame();
#endif // SOFAIMGUI_FORCE_OPENGL2 == 1

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

    static constexpr auto windowNameViewport = ICON_FA_DICE_D6 "  Viewport";
    static constexpr auto windowNamePerformances = ICON_FA_CHART_LINE "  Performances";
    static constexpr auto windowNameProfiler = ICON_FA_HOURGLASS "  Profiler";
    static constexpr auto windowNameSceneGraph = ICON_FA_SITEMAP "  Scene Graph";
    static constexpr auto windowNameDisplayFlags = ICON_FA_EYE "  Display Flags";
    static constexpr auto windowNamePlugins = ICON_FA_PLUS_CIRCLE "  Plugins";
    static constexpr auto windowNameComponents = ICON_FA_LIST "  Components";
    static constexpr auto windowNameLog = ICON_FA_TERMINAL "  Log";
    static constexpr auto windowNameSettings = ICON_FA_SLIDERS_H "  Settings";

    static auto first_time = true;
    if (first_time)
    {
        first_time = false;

        ImGui::DockBuilderRemoveNode(dockspace_id); // clear any previous layout
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_PassthruCentralNode | ImGuiDockNodeFlags_NoDockingInCentralNode | ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->Size);

        auto dock_id_right = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Right, 0.4f, nullptr, &dockspace_id);
        ImGui::DockBuilderDockWindow(windowNameSceneGraph, dock_id_right);
        auto dock_id_down = ImGui::DockBuilderSplitNode(dockspace_id, ImGuiDir_Down, 0.3f, nullptr, &dockspace_id);
        ImGui::DockBuilderDockWindow(windowNameLog, dock_id_down);
        ImGui::DockBuilderDockWindow(windowNameViewport, dockspace_id);
        ImGui::DockBuilderFinish(dockspace_id);
    }
    ImGui::End();


    const ImGuiIO& io = ImGui::GetIO();

    static bool isViewportWindowOpen = true;
    static bool isPerformancesWindowOpen = false;
    static bool isSceneGraphWindowOpen = true;
    static bool isDisplayFlagsWindowOpen = false;
    static bool isPluginsWindowOpen = false;
    static bool isComponentsWindowOpen = false;
    static bool isLogWindowOpen = true;
    static bool isProfilerOpen = false;
    static bool isSettingsOpen = false;

    static bool showFPSInMenuBar = true;
    static bool showTime = true;

    ImVec2 mainMenuBarSize;

    static bool animate;
    animate = groot->animate_.getValue();

    /***************************************
     * Main menu bar
     **************************************/
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open Simulation"))
            {
                simulation::SceneLoaderFactory::SceneLoaderList* loaders =simulation::SceneLoaderFactory::getInstance()->getEntries();
                std::vector<std::pair<std::string, std::string> > filterList;
                filterList.reserve(loaders->size());
                std::pair<std::string, std::string> allFilters {"SOFA files", {} };
                for (auto it=loaders->begin(); it!=loaders->end(); ++it)
                {
                    const auto filterName = (*it)->getFileTypeDesc();

                    sofa::simulation::SceneLoader::ExtensionList extensions;
                    (*it)->getExtensionList(&extensions);
                    std::string extensionsString;
                    for (auto itExt=extensions.begin(); itExt!=extensions.end(); ++itExt)
                    {
                        extensionsString += *itExt;
                        std::cout << *itExt << std::endl;
                        if (itExt != extensions.end() - 1)
                        {
                            extensionsString += ",";
                        }
                    }

                    filterList.emplace_back(filterName, extensionsString);

                    allFilters.second += extensionsString;
                    if (it != loaders->end()-1)
                    {
                        allFilters.second += ",";
                    }
                }
                std::vector<nfdfilteritem_t> nfd_filters;
                nfd_filters.reserve(filterList.size() + 1);
                for (auto& f : filterList)
                {
                    nfd_filters.push_back({f.first.c_str(), f.second.c_str()});
                }
                nfd_filters.insert(nfd_filters.begin(), {allFilters.first.c_str(), allFilters.second.c_str()});

                nfdchar_t *outPath;
                nfdresult_t result = NFD_OpenDialog(&outPath, nfd_filters.data(), nfd_filters.size(), NULL);
                if (result == NFD_OKAY)
                {
                    if (helper::system::FileSystem::exists(outPath))
                    {
                        loadFile(baseGUI, groot, outPath);
                    }
                    NFD_FreePath(outPath);
                }
            }

            const auto filename = baseGUI->getFilename();
            if (ImGui::MenuItem(ICON_FA_REDO "  Reload File"))
            {
                if (!filename.empty() && helper::system::FileSystem::exists(filename))
                {
                    msg_info("GUI") << "Reloading file " << filename;
                    loadFile(baseGUI, groot, filename);
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::TextDisabled(filename.c_str());
                ImGui::EndTooltip();
            }

            if (ImGui::MenuItem(ICON_FA_TIMES_CIRCLE "  Close Simulation"))
            {
                sofa::simulation::node::unload(groot);
                baseGUI->setSimulationIsRunning(false);
                sofa::simulation::node::initRoot(baseGUI->getRootNode().get());
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
            if (ImGui::Checkbox(ICON_FA_EXPAND "  Fullscreen", &isFullScreen))
            {
                baseGUI->switchFullScreen();
            }
            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_CAMERA ICON_FA_CROSSHAIRS"  Center Camera"))
            {
                sofa::component::visual::BaseCamera::SPtr camera;
                groot->get(camera);
                if (camera)
                {
                    if( groot->f_bbox.getValue().isValid())
                    {
                        camera->fitBoundingBox(groot->f_bbox.getValue().minBBox(), groot->f_bbox.getValue().maxBBox());
                    }
                    else
                    {
                        msg_error_when(!groot->f_bbox.getValue().isValid(), "GUI") << "Global bounding box is invalid: " << groot->f_bbox.getValue();
                    }
                }
            }

            const std::string viewFileName = baseGUI->getFilename() + VIEW_FILE_EXTENSION;
            if (ImGui::MenuItem(ICON_FA_CAMERA ICON_FA_ARROW_RIGHT"  Save Camera"))
            {
                sofa::component::visual::BaseCamera::SPtr camera;
                groot->get(camera);
                if (camera)
                {
                    if (camera->exportParametersInFile(viewFileName))
                    {
                        msg_info("GUI") << "Current camera parameters have been exported to "<< viewFileName << " .";
                    }
                    else
                    {
                        msg_error("GUI") << "Could not export camera parameters to " << viewFileName << " .";
                    }
                }
            }
            bool fileExists = sofa::helper::system::FileSystem::exists(viewFileName);
            ImGui::BeginDisabled(!fileExists);
            if (ImGui::MenuItem(ICON_FA_CAMERA ICON_FA_ARROW_LEFT"  Restore Camera"))
            {
                sofa::component::visual::BaseCamera::SPtr camera;
                groot->get(camera);
                if (camera)
                {
                    if (camera->importParametersFromFile(viewFileName))
                    {
                        msg_info("GUI") << "Current camera parameters have been imported from " << viewFileName << " .";
                    }
                    else
                    {
                        msg_error("GUI") << "Could not import camera parameters from " << viewFileName << " .";
                    }
                }
            }

            ImGui::EndDisabled();

            ImGui::Separator();
            if (ImGui::MenuItem(ICON_FA_SAVE"  Save Screenshot"))
            {
                nfdchar_t *outPath;
                std::array<nfdfilteritem_t, 1> filterItem{ {"Image", "jpg,png"} };
                auto sceneFilename = baseGUI->getFilename();
                if (!sceneFilename.empty())
                {
                    std::filesystem::path path(sceneFilename);
                    path = path.replace_extension(".png");
                    sceneFilename = path.filename().string();
                }

                nfdresult_t result = NFD_SaveDialog(&outPath,
                    filterItem.data(), filterItem.size(), nullptr, sceneFilename.c_str());
                if (result == NFD_OKAY)
                {
                    helper::io::STBImage image;
                    image.init(m_currentFBOSize.first, m_currentFBOSize.second, 1, 1, sofa::helper::io::Image::DataType::UINT32, sofa::helper::io::Image::ChannelFormat::RGBA);

                    glBindTexture(GL_TEXTURE_2D, m_fbo->getColorTexture());

                    // Read the pixel data from the OpenGL texture
                    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.getPixels());

                    glBindTexture(GL_TEXTURE_2D, 0);

                    image.save(outPath, 90);
                }
            }

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows"))
        {
            ImGui::Checkbox(windowNameViewport, &isViewportWindowOpen);
            ImGui::Checkbox(windowNamePerformances, &isPerformancesWindowOpen);
            ImGui::Checkbox(windowNameProfiler, &isProfilerOpen);
            ImGui::Checkbox(windowNameSceneGraph, &isSceneGraphWindowOpen);
            ImGui::Checkbox(windowNameDisplayFlags, &isDisplayFlagsWindowOpen);
            ImGui::Checkbox(windowNamePlugins, &isPluginsWindowOpen);
            ImGui::Checkbox(windowNameComponents, &isComponentsWindowOpen);
            ImGui::Checkbox(windowNameLog, &isLogWindowOpen);
            ImGui::Separator();
            ImGui::Checkbox(windowNameSettings, &isSettingsOpen);
            ImGui::EndMenu();
        }

        ImGui::SetCursorPosX(ImGui::GetColumnWidth() / 2); //approximatively the center of the menu bar
        if (ImGui::Button(animate ? ICON_FA_PAUSE : ICON_FA_PLAY))
        {
            sofa::helper::getWriteOnlyAccessor(groot->animate_).wref() = !animate;
        }
        ImGui::SameLine();
        if (animate)
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        }
        if (ImGui::Button(ICON_FA_STEP_FORWARD))
        {
            if (!animate)
            {
                sofa::helper::AdvancedTimer::begin("Animate");

                sofa::simulation::node::animate(groot.get(), groot->getDt());
                sofa::simulation::node::updateVisual(groot.get());

                sofa::helper::AdvancedTimer::end("Animate");
            }
        }
        if (animate)
        {
            ImGui::PopItemFlag();
            ImGui::PopStyleVar();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_REDO_ALT))
        {
            groot->setTime(0.);
            sofa::simulation::node::reset ( groot.get() );
        }

        const auto posX = ImGui::GetCursorPosX();
        if (showFPSInMenuBar)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize("1000.0 FPS ").x
                 - 2 * ImGui::GetStyle().ItemSpacing.x);
            ImGui::Text("%.1f FPS", io.Framerate);
            ImGui::SetCursorPosX(posX);
        }
        if (showTime)
        {
            auto position = ImGui::GetCursorPosX() + ImGui::GetColumnWidth() - ImGui::CalcTextSize("Time: 000.000  ").x
                - 2 * ImGui::GetStyle().ItemSpacing.x;
            if (showFPSInMenuBar)
                position -= ImGui::CalcTextSize("1000.0 FPS ").x;
            ImGui::SetCursorPosX(position);
            ImGui::TextDisabled("Time: %.3f", groot->getTime());
            ImGui::SetCursorPosX(posX);
        }
        mainMenuBarSize = ImGui::GetWindowSize();
        ImGui::EndMainMenuBar();
    }

    /***************************************
     * Viewport window
     **************************************/
    showViewport(groot, windowNameViewport, isViewportWindowOpen);

    /***************************************
     * Performances window
     **************************************/
    Performances::showPerformances(windowNamePerformances, io, isPerformancesWindowOpen);


    /***************************************
     * Profiler window
     **************************************/
    sofa::helper::AdvancedTimer::setEnabled("Animate", isProfilerOpen);
    sofa::helper::AdvancedTimer::setInterval("Animate", 1);
    sofa::helper::AdvancedTimer::setOutputType("Animate", "gui");

    Profiler::showProfiler(groot, windowNameProfiler, isProfilerOpen);

    /***************************************
     * Scene graph window
     **************************************/
    static std::set<core::objectmodel::BaseObject*> openedComponents;
    static std::set<core::objectmodel::BaseObject*> focusedComponents;
    SceneGraph::showSceneGraph(groot, windowNameSceneGraph, isSceneGraphWindowOpen, openedComponents, focusedComponents);


    /***************************************
     * Display flags window
     **************************************/
    DisplayFlags::showDisplayFlags(groot, windowNameDisplayFlags, isDisplayFlagsWindowOpen);

    /***************************************
     * Plugins window
     **************************************/
    showPlugins(windowNamePlugins, isPluginsWindowOpen);

    /***************************************
     * Components window
     **************************************/
    showComponents(windowNameComponents, isComponentsWindowOpen);

    /***************************************
     * Log window
     **************************************/
    Log::showLog(windowNameLog, isLogWindowOpen);

    /***************************************
     * Settings window
     **************************************/
    showSettings(windowNameSettings, isSettingsOpen);

    ImGui::Render();
#if SOFAIMGUI_FORCE_OPENGL2 == 1
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
#else
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
#endif // SOFAIMGUI_FORCE_OPENGL2 == 1

    // Update and Render additional Platform Windows
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void ImGuiGUIEngine::beforeDraw(GLFWwindow*)
{
    glClearColor(0,0,0,1);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_fbo)
    {
        m_fbo = std::make_unique<sofa::gl::FrameBufferObject>();
        m_currentFBOSize = {500, 500};
        m_fbo->init(m_currentFBOSize.first, m_currentFBOSize.second);
    }
    else
    {
        if (m_currentFBOSize.first != static_cast<unsigned int>(m_viewportWindowSize.first)
            || m_currentFBOSize.second != static_cast<unsigned int>(m_viewportWindowSize.second))
        {
            m_fbo->setSize(static_cast<unsigned int>(m_viewportWindowSize.first), static_cast<unsigned int>(m_viewportWindowSize.second));
            m_currentFBOSize = {static_cast<unsigned int>(m_viewportWindowSize.first), static_cast<unsigned int>(m_viewportWindowSize.second)};
        }
    }
    sofa::core::visual::VisualParams::defaultInstance()->viewport() = {0,0,m_currentFBOSize.first, m_currentFBOSize.second};

    m_fbo->start();
}

void ImGuiGUIEngine::afterDraw()
{
    m_fbo->stop();
}

void ImGuiGUIEngine::terminate()
{
    NFD_Quit();

#if SOFAIMGUI_FORCE_OPENGL2 == 1
    ImGui_ImplOpenGL2_Shutdown();
#else
    ImGui_ImplOpenGL3_Shutdown();
#endif // SOFAIMGUI_FORCE_OPENGL2 == 1

    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
}

bool ImGuiGUIEngine::dispatchMouseEvents()
{
    return !ImGui::GetIO().WantCaptureMouse || isMouseOnViewport;
}

} //namespace sofaimgui
