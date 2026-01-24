/*
 * File:        guiproject.cpp
 * Module:      orc-gui
 * Purpose:     GUI project management
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "guiproject.h"
#include "tbc_video_field_representation.h"
#include "logging.h"
#include "presenters/include/project_presenter.h"
#include <QFileInfo>
#include <algorithm>

// Note: DAG is forward-declared in header, actual type comes through presenter

// Helper to convert between presenter and core enums
namespace {
    orc::presenters::VideoFormat toPresenterFormat(orc::VideoSystem sys) {
        switch (sys) {
            case orc::VideoSystem::NTSC: return orc::presenters::VideoFormat::NTSC;
            case orc::VideoSystem::PAL: return orc::presenters::VideoFormat::PAL;
            default: return orc::presenters::VideoFormat::Unknown;
        }
    }
    
    orc::presenters::SourceType toPresenterSource(orc::SourceType src) {
        switch (src) {
            case orc::SourceType::Composite: return orc::presenters::SourceType::Composite;
            case orc::SourceType::YC: return orc::presenters::SourceType::YC;
            default: return orc::presenters::SourceType::Unknown;
        }
    }
}

GUIProject::GUIProject()
    : presenter_(std::make_unique<orc::presenters::ProjectPresenter>())
{
}

GUIProject::~GUIProject() = default;

QString GUIProject::projectName() const
{
    if (!presenter_) return QString();
    
    if (project_path_.isEmpty()) {
        return QString::fromStdString(presenter_->getProjectName());
    }
    return QFileInfo(project_path_).completeBaseName();
}

bool GUIProject::isModified() const
{
    return presenter_ ? presenter_->isModified() : false;
}

void GUIProject::setModified(bool modified)
{
    // Can't directly set modified flag in presenter
    // Modifications are tracked automatically
    (void)modified;
}

bool GUIProject::newEmptyProject(const QString& project_name, orc::VideoSystem video_format, orc::SourceType source_format, QString* error)
{
    try {
        // Create new presenter with empty project
        presenter_ = std::make_unique<orc::presenters::ProjectPresenter>();
        
        // Set project metadata
        presenter_->setProjectName(project_name.toStdString());
        presenter_->setVideoFormat(toPresenterFormat(video_format));
        presenter_->setSourceType(toPresenterSource(source_format));
        
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString("Failed to create project: %1").arg(e.what());
        }
        return false;
    }
}

bool GUIProject::saveToFile(const QString& path, QString* error)
{
    try {
        if (!presenter_) {
            throw std::runtime_error("No project to save");
        }
        
        presenter_->saveProject(path.toStdString());
        project_path_ = path;
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString("Failed to save project: %1").arg(e.what());
        }
        return false;
    }
}

bool GUIProject::loadFromFile(const QString& path, QString* error)
{
    try {
        ORC_LOG_DEBUG("Loading project from: {}", path.toStdString());
        
        // Create new presenter and load project
        presenter_ = std::make_unique<orc::presenters::ProjectPresenter>(path.toStdString());
        project_path_ = path;
        
        ORC_LOG_DEBUG("Building DAG from project");
        auto dag_handle = presenter_->buildDAG();
        
        // Validate DAG was built successfully
        if (hasSource() && !dag_handle) {
            // Project has source nodes but DAG build failed - this is an error
            throw std::runtime_error("Failed to build DAG from project - check that all source files are valid");
        }
        
        return true;
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to load project: {}", e.what());
        if (error) {
            *error = QString("Failed to load project: %1").arg(e.what());
        }
        return false;
    }
}

void GUIProject::clear()
{
    presenter_ = std::make_unique<orc::presenters::ProjectPresenter>();
    dag_cache_.reset();
    project_path_.clear();
}

bool GUIProject::hasSource() const
{
    if (!presenter_) return false;
    
    auto nodes = presenter_->getNodes();
    auto all_stages = orc::presenters::ProjectPresenter::getAllStages();
    
    return std::any_of(nodes.begin(), nodes.end(), 
        [&all_stages](const auto& node) {
            auto stage_it = std::find_if(all_stages.begin(), all_stages.end(),
                [&node](const orc::presenters::StageInfo& s) { return s.name == node.stage_name; });
            return stage_it != all_stages.end() && stage_it->is_source;
        });
}

QString GUIProject::getSourceName() const
{
    if (!presenter_) return QString();
    
    auto nodes = presenter_->getNodes();
    auto all_stages = orc::presenters::ProjectPresenter::getAllStages();
    
    for (const auto& node : nodes) {
        auto stage_it = std::find_if(all_stages.begin(), all_stages.end(),
            [&node](const orc::presenters::StageInfo& s) { return s.name == node.stage_name; });
        if (stage_it != all_stages.end() && stage_it->is_source) {
            return QString::fromStdString(node.label.empty() ? node.stage_name : node.label);
        }
    }
    return QString();
}

std::shared_ptr<orc::DAG> GUIProject::getDAG() const
{
    if (!presenter_) return nullptr;
    
    // Get DAG from presenter and cache it
    auto dag_handle = presenter_->getDAG();
    if (dag_handle) {
        dag_cache_ = std::static_pointer_cast<orc::DAG>(dag_handle);
    }
    return dag_cache_;
}

void GUIProject::rebuildDAG()
{
    if (!presenter_) return;
    
    dag_cache_.reset();
    
    if (!hasSource()) {
        ORC_LOG_DEBUG("No source in project, skipping DAG build");
        return;
    }
    
    try {
        ORC_LOG_DEBUG("Building DAG via ProjectPresenter");
        auto dag_handle = presenter_->buildDAG();
        
        if (dag_handle) {
            dag_cache_ = std::static_pointer_cast<orc::DAG>(dag_handle);
            ORC_LOG_DEBUG("DAG built successfully from project");
        } else {
            ORC_LOG_ERROR("Failed to build DAG from project");
        }
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to build DAG from project: {}", e.what());
        dag_cache_.reset();
    }
}



