/***************************************************************************
 *   Copyright (c) 2023 David Carter <dcarter@david.carter.ca>             *
 *                                                                         *
 *   This file is part of FreeCAD.                                         *
 *                                                                         *
 *   FreeCAD is free software: you can redistribute it and/or modify it    *
 *   under the terms of the GNU Lesser General Public License as           *
 *   published by the Free Software Foundation, either version 2.1 of the  *
 *   License, or (at your option) any later version.                       *
 *                                                                         *
 *   FreeCAD is distributed in the hope that it will be useful, but        *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of            *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU      *
 *   Lesser General Public License for more details.                       *
 *                                                                         *
 *   You should have received a copy of the GNU Lesser General Public      *
 *   License along with FreeCAD. If not, see                               *
 *   <https://www.gnu.org/licenses/>.                                      *
 *                                                                         *
 **************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
#include <QDirIterator>
#include <QFileInfo>
#include <QString>
#endif

#include <App/Application.h>
#include <Base/FileInfo.h>
#include <Base/Interpreter.h>
#include <Base/Stream.h>


#include "Model.h"
#include "ModelLoader.h"
#include "ModelManager.h"


using namespace Materials;

ModelEntry::ModelEntry(const std::shared_ptr<ModelLibraryLocal>& library,
                       const QString& baseName,
                       const QString& modelName,
                       const QString& dir,
                       const QString& modelUuid,
                       const YAML::Node& modelData)
    : _library(library)
    , _base(baseName)
    , _name(modelName)
    , _directory(dir)
    , _uuid(modelUuid)
    , _model(modelData)
    , _dereferenced(false)
{}

std::unique_ptr<std::map<QString, std::shared_ptr<ModelEntry>>> ModelLoader::_modelEntryMap =
    nullptr;

ModelLoader::ModelLoader(std::shared_ptr<std::map<QString, std::shared_ptr<Model>>> modelMap,
                         std::shared_ptr<std::list<std::shared_ptr<ModelLibraryLocal>>> libraryList)
    : _modelMap(modelMap)
    , _libraryList(libraryList)
{
    loadLibraries();
}

void ModelLoader::addLibrary(std::shared_ptr<ModelLibraryLocal> model)
{
    _libraryList->push_back(model);
}

const QString ModelLoader::getUUIDFromPath(const QString& path)
{
    QFile file(path);
    if (!file.exists()) {
        throw ModelNotFound();
    }

    try {
        Base::FileInfo fi(path.toStdString());
        Base::ifstream str(fi);
        YAML::Node yamlroot = YAML::Load(str);
        std::string base = "Model";
        if (yamlroot["AppearanceModel"]) {
            base = "AppearanceModel";
        }

        const QString uuid = QString::fromStdString(yamlroot[base]["UUID"].as<std::string>());
        return uuid;
    }
    catch (YAML::Exception&) {
        throw ModelNotFound();
    }
}

std::shared_ptr<ModelEntry> ModelLoader::getModelFromPath(std::shared_ptr<ModelLibrary> library,
                                                          const QString& path) const
{
    QFile file(path);
    if (!file.exists()) {
        throw ModelNotFound();
    }

    YAML::Node yamlroot;
    std::string base = "Model";
    std::string uuid;
    std::string name;
    try {
        Base::FileInfo fi(path.toStdString());
        Base::ifstream str(fi);
        yamlroot = YAML::Load(str);
        if (yamlroot["AppearanceModel"]) {
            base = "AppearanceModel";
        }

        uuid = yamlroot[base]["UUID"].as<std::string>();
        name = yamlroot[base]["Name"].as<std::string>();
    }
    catch (YAML::Exception const&) {
        throw InvalidModel();
    }

    auto localLibrary = std::static_pointer_cast<ModelLibraryLocal>(library);
    std::shared_ptr<ModelEntry> model = std::make_shared<ModelEntry>(localLibrary,
                                                                     QString::fromStdString(base),
                                                                     QString::fromStdString(name),
                                                                     path,
                                                                     QString::fromStdString(uuid),
                                                                     yamlroot);

    return model;
}

void ModelLoader::showYaml(const YAML::Node& yaml) const
{
    std::stringstream out;

    out << yaml;
    std::string logData = out.str();
    Base::Console().Log("%s\n", logData.c_str());
}

void ModelLoader::dereference(const QString& uuid,
                              std::shared_ptr<ModelEntry> parent,
                              std::shared_ptr<ModelEntry> child,
                              std::map<std::pair<QString, QString>, QString>* inheritances)
{
    auto parentPtr = parent->getModelPtr();
    auto parentBase = parent->getBase().toStdString();
    auto childYaml = child->getModel();
    auto childBase = child->getBase().toStdString();

    std::set<QString> exclude;
    exclude.insert(QStringLiteral("Name"));
    exclude.insert(QStringLiteral("UUID"));
    exclude.insert(QStringLiteral("URL"));
    exclude.insert(QStringLiteral("Description"));
    exclude.insert(QStringLiteral("DOI"));
    exclude.insert(QStringLiteral("Inherits"));

    auto parentProperties = (*parentPtr)[parentBase];
    auto childProperties = childYaml[childBase];
    for (auto it = childProperties.begin(); it != childProperties.end(); it++) {
        std::string name = it->first.as<std::string>();
        if (exclude.count(QString::fromStdString(name)) == 0) {
            // showYaml(it->second);
            if (!parentProperties[name]) {
                parentProperties[name] = it->second;
                // parentProperties[name]["Inherits"] = childYaml[childBase]["UUID"];
                (*inheritances)[std::pair<QString, QString>(uuid, QString::fromStdString(name))] =
                    yamlValue(childYaml[childBase], "UUID", "");
            }
        }
    }
    // showYaml(*parentPtr);
}


void ModelLoader::dereference(std::shared_ptr<ModelEntry> model,
                              std::map<std::pair<QString, QString>, QString>* inheritances)
{
    // Avoid recursion
    if (model->getDereferenced()) {
        return;
    }

    auto yamlModel = model->getModel();
    auto base = model->getBase().toStdString();
    if (yamlModel[base]["Inherits"]) {
        auto inherits = yamlModel[base]["Inherits"];
        for (auto it = inherits.begin(); it != inherits.end(); it++) {
            QString nodeName = QString::fromStdString((*it)["UUID"].as<std::string>());

            // This requires that all models have already been loaded undereferenced
            try {
                std::shared_ptr<ModelEntry> child = (*_modelEntryMap)[nodeName];
                dereference(model->getUUID(), model, child, inheritances);
            }
            catch (const std::out_of_range&) {
                Base::Console().Log("Unable to find '%s' in model map\n",
                                    nodeName.toStdString().c_str());
            }
        }
    }

    model->markDereferenced();
}

QString ModelLoader::yamlValue(const YAML::Node& node,
                               const std::string& key,
                               const std::string& defaultValue)
{
    if (node[key]) {
        return QString::fromStdString(node[key].as<std::string>());
    }
    return QString::fromStdString(defaultValue);
}

void ModelLoader::addToTree(std::shared_ptr<ModelEntry> model,
                            std::map<std::pair<QString, QString>, QString>* inheritances)
{
    std::set<QString> exclude;
    exclude.insert(QStringLiteral("Name"));
    exclude.insert(QStringLiteral("UUID"));
    exclude.insert(QStringLiteral("URL"));
    exclude.insert(QStringLiteral("Description"));
    exclude.insert(QStringLiteral("DOI"));
    exclude.insert(QStringLiteral("Inherits"));

    auto yamlModel = model->getModel();
    if (!model->getLibrary()->isLocal()) {
        throw InvalidLibrary();
    }
    auto library = model->getLibrary();
    auto base = model->getBase().toStdString();
    auto name = model->getName();
    auto directory = model->getDirectory();
    auto uuid = model->getUUID();

    QString description = yamlValue(yamlModel[base], "Description", "");
    QString url = yamlValue(yamlModel[base], "URL", "");
    QString doi = yamlValue(yamlModel[base], "DOI", "");

    Model::ModelType type =
        (base == "Model") ? Model::ModelType_Physical : Model::ModelType_Appearance;

    Model finalModel(library, type, name, directory, uuid, description, url, doi);

    // Add inheritance list
    if (yamlModel[base]["Inherits"]) {
        auto inherits = yamlModel[base]["Inherits"];
        for (auto it = inherits.begin(); it != inherits.end(); it++) {
            QString nodeName = QString::fromStdString((*it)["UUID"].as<std::string>());

            finalModel.addInheritance(nodeName);
        }
    }

    // Add property list
    auto yamlProperties = yamlModel[base];
    for (auto it = yamlProperties.begin(); it != yamlProperties.end(); it++) {
        std::string propName = it->first.as<std::string>();
        if (exclude.count(QString::fromStdString(propName)) == 0) {
            // showYaml(it->second);
            auto yamlProp = yamlProperties[propName];
            auto propDisplayName = yamlValue(yamlProp, "DisplayName", "");
            auto propType = yamlValue(yamlProp, "Type", "");
            auto propUnits = yamlValue(yamlProp, "Units", "");
            auto propURL = yamlValue(yamlProp, "URL", "");
            auto propDescription = yamlValue(yamlProp, "Description", "");
            // auto inherits = yamlValue(yamlProp, "Inherits", "");

            ModelProperty property(QString::fromStdString(propName),
                                   propDisplayName,
                                   propType,
                                   propUnits,
                                   propURL,
                                   propDescription);

            if (propType == QStringLiteral("2DArray") || propType == QStringLiteral("3DArray")) {
                // Base::Console().Log("Reading columns\n");
                // Read the columns
                auto cols = yamlProp["Columns"];
                for (const auto& col : cols) {
                    std::string colName = col.first.as<std::string>();
                    // Base::Console().Log("\tColumns '%s'\n", colName.c_str());

                    auto colProp = cols[colName];
                    auto colPropDisplayName = yamlValue(colProp, "DisplayName", "");
                    auto colPropType = yamlValue(colProp, "Type", "");
                    auto colPropUnits = yamlValue(colProp, "Units", "");
                    auto colPropURL = yamlValue(colProp, "URL", "");
                    auto colPropDescription = yamlValue(colProp, "Description", "");
                    ModelProperty colProperty(QString::fromStdString(colName),
                                              colPropDisplayName,
                                              colPropType,
                                              colPropUnits,
                                              colPropURL,
                                              colPropDescription);

                    property.addColumn(colProperty);
                }
            }

            auto key = std::pair<QString, QString>(uuid, QString::fromStdString(propName));
            if (inheritances->count(key) > 0) {
                property.setInheritance((*inheritances)[key]);
            }

            finalModel.addProperty(property);
        }
    }

    (*_modelMap)[uuid] = library->addModel(finalModel, directory);
}

void ModelLoader::loadLibrary(std::shared_ptr<ModelLibraryLocal> library)
{
    if (_modelEntryMap == nullptr) {
        _modelEntryMap = std::make_unique<std::map<QString, std::shared_ptr<ModelEntry>>>();
    }

    QDirIterator it(library->getDirectory(), QDirIterator::Subdirectories);
    while (it.hasNext()) {
        auto pathname = it.next();
        QFileInfo file(pathname);
        if (file.isFile()) {
            if (file.suffix().toStdString() == "yml") {
                try {
                    auto model = getModelFromPath(library, file.canonicalFilePath());
                    (*_modelEntryMap)[model->getUUID()] = model;
                    // showYaml(model->getModel());
                }
                catch (InvalidModel const&) {
                    Base::Console().Log("Invalid model '%s'\n", pathname.toStdString().c_str());
                }
            }
        }
    }

    std::map<std::pair<QString, QString>, QString> inheritances;
    for (auto it = _modelEntryMap->begin(); it != _modelEntryMap->end(); it++) {
        dereference(it->second, &inheritances);
    }

    for (auto it = _modelEntryMap->begin(); it != _modelEntryMap->end(); it++) {
        addToTree(it->second, &inheritances);
    }
}

void ModelLoader::loadLibraries()
{
    getModelLibraries();
    if (_libraryList) {
        for (auto it = _libraryList->begin(); it != _libraryList->end(); it++) {
            loadLibrary(*it);
        }
    }
}

void ModelLoader::getModelLibraries()
{
    auto param = App::GetApplication().GetParameterGroupByPath(
        "User parameter:BaseApp/Preferences/Mod/Material/Resources");
    bool useBuiltInMaterials = param->GetBool("UseBuiltInMaterials", true);
    bool useMatFromModules = param->GetBool("UseMaterialsFromWorkbenches", true);
    bool useMatFromConfigDir = param->GetBool("UseMaterialsFromConfigDir", true);
    bool useMatFromCustomDir = param->GetBool("UseMaterialsFromCustomDir", true);

    if (useBuiltInMaterials) {
        QString resourceDir = QString::fromStdString(App::Application::getResourceDir()
                                                     + "/Mod/Material/Resources/Models");
        auto libData = std::make_shared<ModelLibraryLocal>(QStringLiteral("System"),
                                                      resourceDir,
                                                      QStringLiteral(":/icons/freecad.svg"));
        _libraryList->push_back(libData);
    }

    if (useMatFromModules) {
        auto moduleParam = App::GetApplication().GetParameterGroupByPath(
            "User parameter:BaseApp/Preferences/Mod/Material/Resources/Modules");
        for (auto& group : moduleParam->GetGroups()) {
            // auto module = moduleParam->GetGroup(group->GetGroupName());
            auto moduleName = QString::fromStdString(group->GetGroupName());
            auto modelDir = QString::fromStdString(group->GetASCII("ModuleModelDir", ""));
            auto modelIcon = QString::fromStdString(group->GetASCII("ModuleIcon", ""));

            if (modelDir.length() > 0) {
                QDir dir(modelDir);
                if (dir.exists()) {
                    auto libData = std::make_shared<ModelLibraryLocal>(moduleName, modelDir, modelIcon);
                    _libraryList->push_back(libData);
                }
            }
        }
    }

    if (useMatFromConfigDir) {
        QString resourceDir =
            QString::fromStdString(App::Application::getUserAppDataDir() + "/Models");
        if (!resourceDir.isEmpty()) {
            QDir materialDir(resourceDir);
            if (materialDir.exists()) {
                auto libData = std::make_shared<ModelLibraryLocal>(
                    QStringLiteral("User"),
                    resourceDir,
                    QStringLiteral(":/icons/preferences-general.svg"));
                _libraryList->push_back(libData);
            }
        }
    }

    if (useMatFromCustomDir) {
        QString resourceDir = QString::fromStdString(param->GetASCII("CustomMaterialsDir", ""));
        if (!resourceDir.isEmpty()) {
            QDir materialDir(resourceDir);
            if (materialDir.exists()) {
                auto libData = std::make_shared<ModelLibraryLocal>(QStringLiteral("Custom"),
                                                              resourceDir,
                                                              QStringLiteral(":/icons/user.svg"));
                _libraryList->push_back(libData);
            }
        }
    }
}
