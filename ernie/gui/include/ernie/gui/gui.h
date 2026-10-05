#pragma once

/// Ernie's GUI viewer in one include, with Dear ImGui and ImPlot for drawing panels:
///
///     #include "ernie/gui/gui.h"
///
///     class SimPanel final : public ernie::gui::Panel {
///       public:
///         SimPanel() : Panel("Sim", ernie::gui::Dock::Left) {}
///         void Draw(ernie::gui::Viewer& viewer) override {
///             ImGui::SliderFloat("Speed", &speed_, 0.0f, 10.0f);
///         }
///       private:
///         float speed_ = 1.0f;
///     };
///
///     ernie::Scene scene;
///     ernie::gui::Viewer viewer(scene);
///     viewer.AddPanel<SimPanel>();
///     viewer.OnPreRender([](ernie::gui::Viewer& v, const ernie::gui::FrameInfo& frame) { ... });
///     viewer.Run();

#include "ernie/gui/icons.h"
#include "ernie/gui/panel.h"
#include "ernie/gui/theme.h"
#include "ernie/gui/viewer.h"

#include <imgui.h>
#include <implot.h>
