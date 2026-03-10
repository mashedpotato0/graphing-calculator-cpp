#include "ast.hpp"
#include "evaluator.hpp"
#include <cairo/cairo.h>
#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

// core plotter class
class Plotter {
public:
  // expression structure
  struct PlotExpr {
    std::unique_ptr<expr> ast;
    double r = 0.96, g = 0.62, b = 0.04; // default orange color
    bool visible = true;
    bool has_y = false;    // depends on y
    bool is_polar = false; // polar coordinate flag
    double theta_min = 0, theta_max = 2 * M_PI;
    std::string label; // original label text
  };

  // graphing area settings
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
    // manual step overrides
    double x_step = 0;
    double y_step = 0;
    std::string x_label = "";
    std::string y_label = "";
  };

  double center_x = 0, center_y = 0;
  double zoom_x = 50, zoom_y = 50;
  std::vector<PlotExpr> expressions;
  PlotSettings settings;

  // rendering entry point
  void render(cairo_t *cr, int width, int height, evaluator &eval) {
    // clear background
    cairo_set_source_rgb(cr, 0.05, 0.07, 0.1);
    cairo_paint(cr);

    // coordinate conversion helpers
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

    // draw main axes
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

    // draw numbers
    cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12.0);

    double x_start = to_math_x(0);
    double x_end = to_math_x(width);
    double y_start = to_math_y(height);
    double y_end = to_math_y(0);

    // horizontal axis ticks
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

    // vertical axis ticks
    if (settings.show_axis_numbers) {
      double step_y =
          (settings.y_step > 0) ? settings.y_step : get_grid_step(zoom_y);
      for (double y = std::floor(y_start / step_y) * step_y; y <= y_end;
           y += step_y) {
        if (std::abs(y) < 1e-9)
          continue;
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

    // axis labels
    if (!settings.x_label.empty()) {
      cairo_move_to(cr, width - 50, sy0 - 10);
      cairo_show_text(cr, settings.x_label.c_str());
    }
    if (!settings.y_label.empty()) {
      cairo_move_to(cr, sx0 + 10, 20);
      cairo_show_text(cr, settings.y_label.c_str());
    }

    // axis arrows
    if (settings.show_arrows) {
      cairo_set_source_rgb(cr, 0.3, 0.4, 0.6);
      if (sx0 >= 0 && sx0 <= width) {
        cairo_move_to(cr, sx0, 0);
        cairo_line_to(cr, sx0 - 5, 10);
        cairo_move_to(cr, sx0, 0);
        cairo_line_to(cr, sx0 + 5, 10);
        cairo_stroke(cr);
      }
      if (sy0 >= 0 && sy0 <= height) {
        cairo_move_to(cr, width, sy0);
        cairo_line_to(cr, width - 10, sy0 - 5);
        cairo_move_to(cr, width, sy0);
        cairo_line_to(cr, width - 10, sy0 + 5);
        cairo_stroke(cr);
      }
    }

    // draw expressions
    for (const auto &pe : expressions) {
      if (!pe.visible || !pe.ast)
        continue;

      cairo_set_source_rgb(cr, pe.r, pe.g, pe.b);
      cairo_set_line_width(cr, 2.0);

      if (pe.has_y) {
        // recursive quadtree plotter five
        auto eval_fn = [&](double x, double y) -> double {
          eval.set_var("x", x);
          eval.set_var("y", y);
          try {
            return eval.eval(*pe.ast);
          } catch (...) {
            return NAN;
          }
        };

        auto lerp = [](double vL, double vR, double pL, double pR) {
          if (!std::isfinite(vL) || !std::isfinite(vR))
            return pL;
          if (std::abs(vL - vR) < 1e-12)
            return pL;
          return pL + (0 - vL) / (vR - vL) * (pR - pL);
        };

        auto find_boundary = [&](auto &&check_fn, double pFinite, double pNan) {
          double low = 0.0, high = 1.0;
          for (int iter = 0; iter < 10; ++iter) {
            double mid = (low + high) / 2.0;
            double pMid = pFinite + mid * (pNan - pFinite);
            if (std::isfinite(check_fn(pMid)))
              low = mid;
            else
              high = mid;
          }
          return pFinite + low * (pNan - pFinite);
        };

        std::function<void(double, double, double, int)> trace_recursive;
        trace_recursive = [&](double i, double j, double size, int depth) {
          double u00 = eval_fn(to_math_x(i), to_math_y(j));
          double u10 = eval_fn(to_math_x(i + size), to_math_y(j));
          double u11 = eval_fn(to_math_x(i + size), to_math_y(j + size));
          double u01 = eval_fn(to_math_x(i), to_math_y(j + size));

          bool has_nan = !std::isfinite(u00) || !std::isfinite(u10) ||
                         !std::isfinite(u11) || !std::isfinite(u01);
          bool all_nan = !std::isfinite(u00) && !std::isfinite(u10) &&
                         !std::isfinite(u11) && !std::isfinite(u01);

          int index = (std::isfinite(u00) && u00 > 0 ? 1 : 0) |
                      (std::isfinite(u10) && u10 > 0 ? 2 : 0) |
                      (std::isfinite(u11) && u11 > 0 ? 4 : 0) |
                      (std::isfinite(u01) && u01 > 0 ? 8 : 0);

          bool sign_change = (index != 0 && index != 15);

          // recursion depth and size checks
          if (depth < 8 && size > 2.0) {
            bool should_recurse = sign_change || (has_nan && !all_nan);

            // test midpoint for hidden features
            if (!should_recurse) {
              double um =
                  eval_fn(to_math_x(i + size / 2.0), to_math_y(j + size / 2.0));
              if (std::isfinite(um) && (um > 0) != (u00 > 0))
                should_recurse = true;
              else if (!std::isfinite(um) != all_nan)
                should_recurse = true;
            }

            if (should_recurse) {
              double half = size / 2.0;
              trace_recursive(i, j, half, depth + 1);
              trace_recursive(i + half, j, half, depth + 1);
              trace_recursive(i, j + half, half, depth + 1);
              trace_recursive(i + half, j + half, half, depth + 1);
              return;
            }
          }

          // draw leaf
          if (sign_change || (has_nan && !all_nan)) {
            struct Point {
              double x, y;
            };
            auto get_pt = [&](int edge) -> Point {
              switch (edge) {
              case 0:
                return {lerp(u00, u10, i, i + size), (double)j};
              case 1:
                return {(double)i + size, lerp(u10, u11, j, j + size)};
              case 2:
                return {lerp(u01, u11, i, i + size), (double)j + size};
              case 3:
                return {(double)i, lerp(u00, u01, j, j + size)};
              default:
                return {0, 0};
              }
            };

            auto draw_seg = [&](int e1, int e2) {
              bool ok1 = false, ok2 = false;
              if (e1 == 0)
                ok1 = std::isfinite(u00) && std::isfinite(u10);
              else if (e1 == 1)
                ok1 = std::isfinite(u10) && std::isfinite(u11);
              else if (e1 == 2)
                ok1 = std::isfinite(u01) && std::isfinite(u11);
              else if (e1 == 3)
                ok1 = std::isfinite(u00) && std::isfinite(u01);

              if (e2 == 0)
                ok2 = std::isfinite(u00) && std::isfinite(u10);
              else if (e2 == 1)
                ok2 = std::isfinite(u10) && std::isfinite(u11);
              else if (e2 == 2)
                ok2 = std::isfinite(u01) && std::isfinite(u11);
              else if (e2 == 3)
                ok2 = std::isfinite(u00) && std::isfinite(u01);

              Point p1 = get_pt(e1), p2 = get_pt(e2);
              if (!ok1 || !ok2) {
                if (!ok1) {
                  if (e1 == 0)
                    p1.x = find_boundary(
                        [&](double px) {
                          return eval_fn(to_math_x(px), to_math_y(j));
                        },
                        std::isfinite(u00) ? i : i + size,
                        std::isfinite(u00) ? i + size : i);
                  else if (e1 == 1)
                    p1.y = find_boundary(
                        [&](double py) {
                          return eval_fn(to_math_x(i + size), to_math_y(py));
                        },
                        std::isfinite(u10) ? j : j + size,
                        std::isfinite(u10) ? j + size : j);
                  else if (e1 == 2)
                    p1.x = find_boundary(
                        [&](double px) {
                          return eval_fn(to_math_x(px), to_math_y(j + size));
                        },
                        std::isfinite(u01) ? i : i + size,
                        std::isfinite(u01) ? i + size : i);
                  else if (e1 == 3)
                    p1.y = find_boundary(
                        [&](double py) {
                          return eval_fn(to_math_x(i), to_math_y(py));
                        },
                        std::isfinite(u00) ? j : j + size,
                        std::isfinite(u00) ? j + size : j);
                }
                if (!ok2) {
                  if (e2 == 0)
                    p2.x = find_boundary(
                        [&](double px) {
                          return eval_fn(to_math_x(px), to_math_y(j));
                        },
                        std::isfinite(u00) ? i : i + size,
                        std::isfinite(u00) ? i + size : i);
                  else if (e2 == 1)
                    p2.y = find_boundary(
                        [&](double py) {
                          return eval_fn(to_math_x(i + size), to_math_y(py));
                        },
                        std::isfinite(u10) ? j : j + size,
                        std::isfinite(u10) ? j + size : j);
                  else if (e2 == 2)
                    p2.x = find_boundary(
                        [&](double px) {
                          return eval_fn(to_math_x(px), to_math_y(j + size));
                        },
                        std::isfinite(u01) ? i : i + size,
                        std::isfinite(u01) ? i + size : i);
                  else if (e2 == 3)
                    p2.y = find_boundary(
                        [&](double py) {
                          return eval_fn(to_math_x(i), to_math_y(py));
                        },
                        std::isfinite(u00) ? j : j + size,
                        std::isfinite(u00) ? j + size : j);
                }
              }
              if (std::isfinite(p1.x) && std::isfinite(p1.y) &&
                  std::isfinite(p2.x) && std::isfinite(p2.y)) {
                cairo_move_to(cr, p1.x, p1.y);
                cairo_line_to(cr, p2.x, p2.y);
              }
            };

            auto solve_ambiguous = [&](int type) {
              double center =
                  eval_fn(to_math_x(i + size / 2.0), to_math_y(j + size / 2.0));
              if (type == 5) { // case 5 0 1 are positive
                if (center > 0) {
                  draw_seg(0, 3);
                  draw_seg(1, 2);
                } else {
                  draw_seg(0, 1);
                  draw_seg(2, 3);
                }
              } else { // case 10 1 3 are positive
                if (center > 0) {
                  draw_seg(0, 1);
                  draw_seg(2, 3);
                } else {
                  draw_seg(0, 3);
                  draw_seg(1, 2);
                }
              }
            };

            switch (index) {
            case 1:
            case 14:
              draw_seg(0, 3);
              break;
            case 2:
            case 13:
              draw_seg(0, 1);
              break;
            case 3:
            case 12:
              draw_seg(1, 3);
              break;
            case 4:
            case 11:
              draw_seg(1, 2);
              break;
            case 5:
              solve_ambiguous(5);
              break;
            case 6:
            case 9:
              draw_seg(0, 2);
              break;
            case 7:
            case 8:
              draw_seg(2, 3);
              break;
            case 10:
              solve_ambiguous(10);
              break;
            }
          }
        };

        const double CHUNK_SIZE = 64.0;
        for (double gy = 0; gy < height; gy += CHUNK_SIZE) {
          for (double gx = 0; gx < width; gx += CHUNK_SIZE) {
            trace_recursive(gx, gy, CHUNK_SIZE, 0);
          }
        }
        cairo_stroke(cr);
      } else if (pe.is_polar) {
        // polar plots
        bool first = true;
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
        // standard f(x) functions
        bool first = true;
        double prev_y = 0.0;
        for (int i = 0; i <= width; ++i) {
          double x = to_math_x(i);
          eval.set_var("x", x);
          try {
            double y = eval.eval(*pe.ast);
            if (std::isfinite(y)) {
              double sy = to_screen_y(y);

              // skip discontinuities
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

      // curve identification labels
      if (!pe.label.empty()) {
        double lx = 0, ly = 0;
        bool found = false;

        if (pe.has_y) {
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
  // grid step calculation
  double get_grid_step(double zoom) {
    double pixels_per_step = 80.0;
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

  // draw gridlines
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

    for (double x = std::floor(x_start / step_x) * step_x; x <= x_end;
         x += step_x) {
      double sx = to_screen_x(x);
      cairo_move_to(cr, sx, 0);
      cairo_line_to(cr, sx, height);
      cairo_stroke(cr);
    }
    for (double y = std::floor(y_start / step_y) * step_y; y <= y_end;
         y += step_y) {
      double sy = to_screen_y(y);
      cairo_move_to(cr, 0, sy);
      cairo_line_to(cr, width, sy);
      cairo_stroke(cr);
    }

    // draw minor gridlines
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
