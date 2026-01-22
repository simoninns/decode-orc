/*
 * File:        render_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Rendering presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "render_presenter.h"
#include "../core/include/project.h"
#include <stdexcept>

namespace orc::presenters {

class RenderPresenter::Impl {
public:
    explicit Impl(orc::Project* project)
        : project_(project)
        , is_exporting_(false)
    {
        if (!project_) {
            throw std::invalid_argument("Project cannot be null");
        }
    }
    
    orc::Project* project_;
    bool is_exporting_;
    RenderProgress export_progress_;
};

RenderPresenter::RenderPresenter(orc::Project* project)
    : impl_(std::make_unique<Impl>(project))
{
}

RenderPresenter::~RenderPresenter() = default;

RenderPresenter::RenderPresenter(RenderPresenter&&) noexcept = default;
RenderPresenter& RenderPresenter::operator=(RenderPresenter&&) noexcept = default;

PreviewImage RenderPresenter::renderPreview(orc::NodeID node_id, orc::FieldID field_id)
{
    return PreviewImage{};
}

PreviewImage RenderPresenter::renderPreviewFirst(orc::NodeID node_id)
{
    return PreviewImage{};
}

size_t RenderPresenter::getFieldCount(orc::NodeID node_id) const
{
    return 0;
}

std::vector<orc::FieldID> RenderPresenter::getAvailableFields(orc::NodeID node_id) const
{
    return {};
}

bool RenderPresenter::exportSequence(orc::NodeID node_id, const ExportOptions& options)
{
    return false;
}

void RenderPresenter::cancelExport()
{
    if (impl_->is_exporting_) {
        impl_->is_exporting_ = false;
    }
}

bool RenderPresenter::isExporting() const
{
    return impl_->is_exporting_;
}

RenderProgress RenderPresenter::getExportProgress() const
{
    return impl_->export_progress_;
}

void RenderPresenter::clearCache()
{
}

std::string RenderPresenter::getCacheStats() const
{
    return "Cache: empty";
}

bool RenderPresenter::getImageDimensions(orc::NodeID node_id, int& width, int& height) const
{
    width = 0;
    height = 0;
    return false;
}

} // namespace orc::presenters
