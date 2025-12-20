# Preview Renderer API Usage

## Overview

The `PreviewRenderer` class in orc-core provides a complete rendering solution for the GUI. The GUI is now responsible **only** for displaying images - all processing, field weaving, and sample conversion happens in core.

## Architecture

```
GUI (orc-gui)                    Core (orc-core)
┌─────────────────┐            ┌──────────────────────┐
│                 │            │                      │
│ User Interaction│───────────>│  PreviewRenderer     │
│ - Select node   │  Query API │  - Get outputs       │
│ - Select type   │            │  - Render outputs    │
│ - Select index  │            │                      │
│                 │<───────────│  Returns RGB888      │
│                 │  RGB Data  │                      │
│                 │            │                      │
│ QImage display  │            │  DAGFieldRenderer    │
│ (aspect ratio)  │            │  VideoFieldRepr      │
│                 │            │  Field data access   │
└─────────────────┘            └──────────────────────┘
```

## API Usage

### 1. Initialize Preview Renderer

```cpp
// In mainwindow.cpp or similar
#include "preview_renderer.h"

// Create preview renderer with project DAG
preview_renderer_ = std::make_unique<orc::PreviewRenderer>(project_->dag());
```

### 2. Query Available Outputs

When user selects a node, query what outputs are available:

```cpp
std::string node_id = "node_1";
auto outputs = preview_renderer_->get_available_outputs(node_id);

for (const auto& output : outputs) {
    std::cout << output.display_name << ": " << output.count << " available\n";
}

// Example output:
// Field: 400 available
// Frame (Even First): 200 available  
// Frame (Odd First): 200 available
// Luma: 400 available
```

### 3. Populate GUI Controls

Use the output info to populate dropdowns, spinboxes, etc:

```cpp
// Clear and repopulate output type combo box
outputTypeCombo->clear();
for (const auto& output : outputs) {
    if (output.is_available) {
        outputTypeCombo->addItem(
            QString::fromStdString(output.display_name),
            static_cast<int>(output.type)
        );
    }
}

// Set max value for index spinner
auto selected_type = get_selected_output_type();
auto max_count = preview_renderer_->get_output_count(node_id, selected_type);
indexSpinner->setMaximum(max_count > 0 ? max_count - 1 : 0);
```

### 4. Render and Display

When user requests a preview (changes index, type, or node):

```cpp
void MainWindow::updatePreview() {
    std::string node_id = current_view_node_id_;
    orc::PreviewOutputType type = get_selected_output_type();
    uint64_t index = indexSpinner->value();
    
    // Render in core
    auto result = preview_renderer_->render_output(node_id, type, index);
    
    if (!result.success) {
        // Show error
        statusBar()->showMessage(QString::fromStdString(result.error_message));
        return;
    }
    
    // Convert RGB888 to QImage (zero-copy if possible)
    const auto& img = result.image;
    QImage qimage(
        img.rgb_data.data(),
        img.width,
        img.height,
        img.width * 3,  // bytes per line
        QImage::Format_RGB888
    );
    
    // Display (make a deep copy since rgb_data is temporary)
    preview_widget_->setPixmap(QPixmap::fromImage(qimage.copy()));
}
```

### 5. Export to PNG

Save the current preview to a PNG file:

```cpp
void MainWindow::onExportPNG() {
    QString filename = QFileDialog::getSaveFileName(
        this,
        "Export Preview as PNG",
        QString(),
        "PNG Images (*.png)"
    );
    
    if (filename.isEmpty()) {
        return;
    }
    
    std::string node_id = current_view_node_id_;
    orc::PreviewOutputType type = get_selected_output_type();
    uint64_t index = indexSpinner->value();
    
    // Export directly from core (no GUI involvement in rendering)
    bool success = preview_renderer_->save_png(
        node_id,
        type,
        index,
        filename.toStdString()
    );
    
    if (success) {
        statusBar()->showMessage("Exported to " + filename);
    } else {
        QMessageBox::warning(this, "Export Failed", "Failed to save PNG file");
    }
}
```

Or save an already-rendered image:

```cpp
// If you already have a PreviewImage from render_output()
auto result = preview_renderer_->render_output(node_id, type, index);
if (result.success) {
    preview_renderer_->save_png(result.image, "/path/to/output.png");
}
```

### 6. Handle DAG Changes

When the DAG is modified, update the renderer:

```cpp
void MainWindow::onDAGModified() {
    // Update renderer with new DAG
    preview_renderer_->update_dag(project_->dag());
    
    // Refresh available outputs
    refreshOutputList();
    
    // Re-render current view
    updatePreview();
}
```

## Export API

The PreviewRenderer provides two methods for PNG export:

### Direct Export

Render and save in one call:

```cpp
bool save_png(
    const std::string& node_id,
    PreviewOutputType type,
    uint64_t index,
    const std::string& filename
);
```

### Export Rendered Image

Save an already-rendered PreviewImage:

```cpp
bool save_png(const PreviewImage& image, const std::string& filename);
```

Both return `true` on success, `false` on error. PNG files are written using libpng with default compression.

## Output Types

| Type | Description | Index Range |
|------|-------------|-------------|
| `Field` | Single interlaced field | 0 to (field_count - 1) |
| `Frame_EvenOdd` | Frame with even field on even lines | 0 to (field_count / 2 - 1) |
| `Frame_OddEven` | Frame with odd field on even lines | 0 to (field_count / 2 - 1) |
| `Luma` | Luma component only | 0 to (field_count - 1) |
| `Chroma` | Chroma component (future) | TBD |
| `Composite` | Full composite video (future) | TBD |

## Image Format

All rendered images are returned as `PreviewImage`:

```cpp
struct PreviewImage {
    uint32_t width;              // Image width in pixels
    uint32_t height;             // Image height in pixels
    std::vector<uint8_t> rgb_data;  // RGB888 data (width * height * 3 bytes)
};
```

Layout: `[R0 G0 B0 R1 G1 B1 ... Rn Gn Bn]`

## Example: Complete GUI Integration

```cpp
class PreviewWindow : public QMainWindow {
    Q_OBJECT
    
public:
    PreviewWindow(QWidget *parent = nullptr)
        : QMainWindow(parent)
    {
        setupUI();
    }
    
    void setProject(std::shared_ptr<orc::Project> project) {
        project_ = project;
        preview_renderer_ = std::make_unique<orc::PreviewRenderer>(project_->dag());
        refreshNodeList();
    }
    
private slots:
    void onNodeSelected(const QString& node_id) {
        current_node_ = node_id.toStdString();
        refreshOutputTypes();
        updatePreview();
    }
    
    void onOutputTypeChanged(int index) {
        current_type_ = static_cast<orc::PreviewOutputType>(
            typeCombo_->itemData(index).toInt()
        );
        updateMaxIndex();
        updatePreview();
    }
    
    void onIndexChanged(int value) {
        current_index_ = static_cast<uint64_t>(value);
        updatePreview();
    }
    
private:
    void refreshOutputTypes() {
        typeCombo_->clear();
        
        auto outputs = preview_renderer_->get_available_outputs(current_node_);
        for (const auto& output : outputs) {
            typeCombo_->addItem(
                QString::fromStdString(output.display_name),
                static_cast<int>(output.type)
            );
        }
    }
    
    void updateMaxIndex() {
        auto count = preview_renderer_->get_output_count(current_node_, current_type_);
        indexSpinner_->setMaximum(count > 0 ? count - 1 : 0);
    }
    
    void updatePreview() {
        auto result = preview_renderer_->render_output(
            current_node_,
            current_type_,
            current_index_
        );
        
        if (result.success) {
            QImage img(
                result.image.rgb_data.data(),
                result.image.width,
                result.image.height,
                result.image.width * 3,
                QImage::Format_RGB888
            );
            previewLabel_->setPixmap(QPixmap::fromImage(img.copy()));
        } else {
            statusBar()->showMessage(QString::fromStdString(result.error_message));
        }
    }
    
    std::shared_ptr<orc::Project> project_;
    std::unique_ptr<orc::PreviewRenderer> preview_renderer_;
    
    std::string current_node_;
    orc::PreviewOutputType current_type_ = orc::PreviewOutputType::Field;
    uint64_t current_index_ = 0;
    
    QComboBox* typeCombo_;
    QSpinBox* indexSpinner_;
    QLabel* previewLabel_;
};
```

## Benefits

1. **No rendering logic in GUI** - GUI just displays RGB888 data
2. **Consistent rendering** - CLI and GUI use same core code
3. **Easy to extend** - Add new output types (chroma, composite) in one place
4. **Testable** - Core rendering can be unit tested without Qt
5. **Simple GUI code** - GUI focuses on user interaction and display
