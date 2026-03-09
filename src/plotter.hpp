#define _USE_MATH_DEFINES
#include "ast.hpp"
#include "evaluator.hpp"
#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

class Plotter {
public:
  struct PlotExpr {
    std::unique_ptr<expr> ast;
    double r = 0.96, g = 0.62, b = 0.04; // orange
    bool visible = true;
    bool has_y = false;    // if depends on y
    bool is_polar = false; // r = f(theta)
    double theta_min = 0, theta_max = 2 * M_PI;
    std::string label; // orig text
  };

  struct PlotSettings {
    bool show_grid = true;
    bool show_axis_numbers = true;
    bool show_minor_gridlines = true;
    bool show_main_axis = true;
    bool show_arrows = true;
    bool lock_aspect_ratio = true;
    bool lock_viewport = false;
    bool use_radians = true;
    bool x_log_scale = false;
    bool y_log_scale = false;
    // manual step overrides 0=auto
    double x_step = 0;
    double y_step = 0;
    std::string x_label = "";
    std::string y_label = "";
  };

  double center_x = 0.0;
  double center_y = 0.0;
  double zoom_x = 50.0;
  double zoom_y = 50.0;

  std::vector<PlotExpr> expressions;
  PlotSettings settings;

  void render(cairo_t *cr, int width, int height, evaluator &eval) {
    // clear bg
    cairo_set_source_rgb(cr, 0.05, 0.07, 0.1);
    cairo_paint(cr);

    // xform fns
    auto to_screen_x = [&](double x) {
      return width / 2.0 + (x - center_x) * zoom_x;
    };
    auto to_screen_y = [&](double y) {
      return height / 2.0 - (y - center_y) * zoom_y;
    };
    auto to_math_x = [&](double sx) {
      return (sx - width / 2.0) / zoom_x + center_x;
    };
    auto to_math_y = [&](double sy) {
      return (height / 2.0 - sy) / zoom_y + center_y;
    };

    // draw grid
    if (settings.show_grid) {
      draw_grid(cr, width, height, to_screen_x, to_screen_y, to_math_x,
                to_math_y);
    }

    double sx0 = to_screen_x(0);
    double sy0 = to_screen_y(0);

    // draw axes
    if (settings.show_main_axis) {
      cairo_set_source_rgb(cr, 0.3, 0.4, 0.6);
      cairo_set_line_width(cr, 2.0);

      if (sx0 >= 0 && sx0 <= width) {
        cairo_move_to(cr, sx0, 0);
        cairo_line_to(cr, sx0, height);
        cairo_stroke(cr);
      }
      if (sy0 >= 0 && sy0 <= height) {
        cairo_move_to(cr, 0, sy0);
        cairo_line_to(cr, width, sy0);
        cairo_stroke(cr);
      }
    }

    // draw axis labels
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);

    double x_start = to_math_x(0);
    double x_end = to_math_x(width);
    double y_start = to_math_y(height);
    double y_end = to_math_y(0);

    // x axis labels
    if (settings.show_axis_numbers) {
      double step_x =
          (settings.x_step > 0) ? settings.x_step : get_grid_step(zoom_x);
      for (double x = std::floor(x_start / step_x) * step_x; x <= x_end;
           x += step_x) {
        if (std::abs(x) < 1e-9)
          continue; // skip zero
        double sx = to_screen_x(x);
        char buf[32];
        int precision = 0;
        if (step_x < 0.01)
          precision = 4;
        else if (step_x < 0.1)
          precision = 3;
        else if (step_x < 1.0)
          precision = 2;
        else if (step_x < 10.0)
          precision = 1;

        if (std::abs(x) >= 1e6 || (std::abs(x) < 1e-3 && std::abs(x) > 0))
          snprintf(buf, sizeof(buf), "%.1e", x);
        else if (std::abs(x - std::round(x)) < 1e-10)
          snprintf(buf, sizeof(buf), "%d", (int)std::round(x));
        else
          snprintf(buf, sizeof(buf), "%.*f", precision, x);

        cairo_move_to(cr, sx + 2, sy0 + 15);
        cairo_show_text(cr, buf);
      }
    }

    // y axis labels
    if (settings.show_axis_numbers) {
      double step_y =
          (settings.y_step > 0) ? settings.y_step : get_grid_step(zoom_y);
      for (double y = std::floor(y_start / step_y) * step_y; y <= y_end;
           y += step_y) {
        if (std::abs(y) < 1e-9)
          continue; // skip zero
        double sy = to_screen_y(y);
        char buf[32];
        int precision = 0;
        if (step_y < 0.01)
          precision = 4;
        else if (step_y < 0.1)
          precision = 3;
        else if (step_y < 1.0)
          precision = 2;
        else if (step_y < 10.0)
          precision = 1;

        if (std::abs(y - std::round(y)) < 1e-10)
          snprintf(buf, sizeof(buf), "%d", (int)std::round(y));
        else
          snprintf(buf, sizeof(buf), "%.*f", precision, y);

        cairo_text_extents_t ext;
        cairo_text_extents(cr, buf, &ext);
        cairo_move_to(cr, sx0 - ext.width - 5, sy + 5);
        cairo_show_text(cr, buf);
      }
    }

    // draw axis labels (x-label / y-label)
    if (!settings.x_label.empty()) {
      cairo_move_to(cr, width - 50, sy0 - 10);
      cairo_show_text(cr, settings.x_label.c_str());
    }
    if (!settings.y_label.empty()) {
      cairo_move_to(cr, sx0 + 10, 20);
      cairo_show_text(cr, settings.y_label.c_str());
    }

    // draw arrows
    if (settings.show_arrows) {
      cairo_set_source_rgb(cr, 0.3, 0.4, 0.6);
      if (sx0 >= 0 && sx0 <= width) {
        // top arrow
        cairo_move_to(cr, sx0, 0);
        cairo_line_to(cr, sx0 - 5, 10);
        cairo_move_to(cr, sx0, 0);
        cairo_line_to(cr, sx0 + 5, 10);
        cairo_stroke(cr);
      }
      if (sy0 >= 0 && sy0 <= height) {
        // right arrow
        cairo_move_to(cr, width, sy0);
        cairo_line_to(cr, width - 10, sy0 - 5);
        cairo_move_to(cr, width, sy0);
        cairo_line_to(cr, width - 10, sy0 + 5);
        cairo_stroke(cr);
      }
    }

    // draw fns
    for (const auto &pe : expressions) {
      if (!pe.visible || !pe.ast)
        continue;

      cairo_set_source_rgb(cr, pe.r, pe.g, pe.b);
      cairo_set_line_width(cr, 2.0);

      if (pe.has_y) {
        // implicit plot marching squares light
        const int GRID_SIZE = 1; // px step
        for (int i = 0; i < width; i += GRID_SIZE) {
          for (int j = 0; j < height; j += GRID_SIZE) {
            double x1 = to_math_x(i);
            double y1 = to_math_y(j);
            double x2 = to_math_x(i + GRID_SIZE);
            double y2 = to_math_y(j + GRID_SIZE);

            auto eval_at = [&](double x, double y) {
              eval.set_var("x", x);
              eval.set_var("y", y);
              try {
                return eval.eval(*pe.ast);
              } catch (...) {
                return 0.0;
              }
            };

            double v11 = eval_at(x1, y1);
            double v12 = eval_at(x1, y2);
            double v21 = eval_at(x2, y1);
            double v22 = eval_at(x2, y2);

            // sign change check all 4 edges
            bool s11 = v11 > 0;
            bool s12 = v12 > 0;
            bool s21 = v21 > 0;
            bool s22 = v22 > 0;

            if (s11 != s21 || s12 != s22 || s11 != s12 || s21 != s22) {
              cairo_move_to(cr, i, j);
              cairo_line_to(cr, i + GRID_SIZE, j + GRID_SIZE);
            }
          }
        }
        cairo_stroke(cr);
      } else if (pe.is_polar) {
        // polar r = f(theta)
        bool first = true;
        // adaptive res 100 step/rad min 500 max 10000
        double theta_range = std::abs(pe.theta_max - pe.theta_min);
        double steps = std::clamp(theta_range * 100.0, 500.0, 10000.0);
        double d_theta = (pe.theta_max - pe.theta_min) / steps;

        for (int i = 0; i <= (int)steps; ++i) {
          double theta = pe.theta_min + i * d_theta;
          eval.set_var("theta", theta);
          try {
            double r = eval.eval(*pe.ast);
            if (std::isfinite(r)) {
              double x = r * std::cos(theta);
              double y = r * std::sin(theta);
              double sx = to_screen_x(x);
              double sy = to_screen_y(y);

              if (first) {
                cairo_move_to(cr, sx, sy);
                first = false;
              } else {
                cairo_line_to(cr, sx, sy);
              }
            } else {
              first = true;
            }
          } catch (...) {
            first = true;
          }
        }
        cairo_stroke(cr);
      } else {
        bool first = true;
        double prev_y = 0.0;
        for (int i = 0; i <= width; ++i) {
          double x = to_math_x(i);
          eval.set_var("x", x);
          try {
            double y = eval.eval(*pe.ast);
            if (std::isfinite(y)) {
              double sy = to_screen_y(y);

              // discontinuity check e.g. tanx skip line if jump big and sign
              // flip
              bool is_discontinuous =
                  !first && (std::abs(y - prev_y) > (height / zoom_y) * 10.0) &&
                  ((y > 0) != (prev_y > 0));

              if (first || is_discontinuous) {
                cairo_move_to(cr, i, sy);
                first = false;
              } else {
                cairo_line_to(cr, i, sy);
              }
              prev_y = y;
            } else {
              first = true;
            }
          } catch (...) {
            first = true;
          }
        }
        cairo_stroke(cr);
      }

      // draw label nearby
      if (!pe.label.empty()) {
        double lx = 0, ly = 0;
        bool found = false;

        // find rightmost visible point
        if (pe.has_y) {
          // implicit plot pick point on screen
          lx = width - 80;
          ly = height - (40 + (&pe - &expressions[0]) * 20);
          found = true;
        } else {
          for (int i = width; i >= 0; i--) {
            double x = to_math_x(i);
            eval.set_var("x", x);
            try {
              double y = eval.eval(*pe.ast);
              if (std::isfinite(y)) {
                double sy = to_screen_y(y);
                if (sy >= 20 && sy <= height - 20) {
                  lx = i + 5;
                  ly = sy;
                  found = true;
                  break;
                }
              }
            } catch (...) {
            }
          }
        }

        if (found) {
          cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
          cairo_text_extents_t te;
          cairo_text_extents(cr, pe.label.c_str(), &te);
          cairo_rectangle(cr, lx - 2, ly - te.height - 2, te.width + 4,
                          te.height + 4);
          cairo_fill(cr);

          cairo_set_source_rgb(cr, pe.r, pe.g, pe.b);
          cairo_move_to(cr, lx, ly);
          cairo_show_text(cr, pe.label.c_str());
        }
      }
    }
  }

private:
  double get_grid_step(double zoom) {
    double pixels_per_step = 80.0; // desired px between grid lines
    double ideal_step = pixels_per_step / zoom;

    double log_step = std::log10(ideal_step);
    double power = std::floor(log_step);
    double base = std::pow(10, power);
    double factor = ideal_step / base;

    double actual_factor;
    if (factor < 1.5)
      actual_factor = 1.0;
    else if (factor < 3.5)
      actual_factor = 2.0;
    else if (factor < 7.5)
      actual_factor = 5.0;
    else
      actual_factor = 10.0;

    return base * actual_factor;
  }

  template <typename F1, typename F2, typename F3, typename F4>
  void draw_grid(cairo_t *cr, int width, int height, F1 to_screen_x,
                 F2 to_screen_y, F3 to_math_x, F4 to_math_y) {
    cairo_set_source_rgb(cr, 0.1, 0.15, 0.25);
    cairo_set_line_width(cr, 1.0);

    double x_start = to_math_x(0);
    double x_end = to_math_x(width);
    double y_start = to_math_y(height);
    double y_end = to_math_y(0);

    double step_x = get_grid_step(zoom_x);
    double step_y = get_grid_step(zoom_y);

    // x grid
    for (double x = std::floor(x_start / step_x) * step_x; x <= x_end;
         x += step_x) {
      double sx = to_screen_x(x);
      cairo_move_to(cr, sx, 0);
      cairo_line_to(cr, sx, height);
      cairo_stroke(cr);
    }
    // y grid
    for (double y = std::floor(y_start / step_y) * step_y; y <= y_end;
         y += step_y) {
      double sy = to_screen_y(y);
      cairo_move_to(cr, 0, sy);
      cairo_line_to(cr, width, sy);
      cairo_stroke(cr);
    }

    // minor gridlines
    if (settings.show_minor_gridlines) {
      cairo_set_source_rgba(cr, 0.1, 0.15, 0.25, 0.4);
      double mstep_x = step_x / 5.0;
      double mstep_y = step_y / 5.0;
      for (double x = std::floor(x_start / mstep_x) * mstep_x; x <= x_end;
           x += mstep_x) {
        double sx = to_screen_x(x);
        cairo_move_to(cr, sx, 0);
        cairo_line_to(cr, sx, height);
        cairo_stroke(cr);
      }
      for (double y = std::floor(y_start / mstep_y) * mstep_y; y <= y_end;
           y += mstep_y) {
        double sy = to_screen_y(y);
        cairo_move_to(cr, 0, sy);
        cairo_line_to(cr, width, sy);
        cairo_stroke(cr);
      }
    }
  }
};
