#define _USE_MATH_DEFINES
#include "ast.hpp"
#include "evaluator.hpp"
#include "lexer.hpp"
#include "math_editor.hpp"
#include "parser.hpp"
#include "plotter.hpp"
#include "renderer.hpp"
#include <cmath>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <iostream>
#include <memory>
#include <vector>

static function_registry reg;
static evaluator eval_engine(reg);
static Plotter plotter;
static std::vector<GtkWidget *> equation_entries;
static GtkWidget *equations_vbox;
static bool dragging = false;
static double drag_start_center_x, drag_start_center_y;

// views for ui
static double x_axis_min = -10, x_axis_max = 10;
static double y_axis_min = -10, y_axis_max = 10;

// animation state
struct SliderAnimData {
  GtkRange *range;
  double min_val, max_val, step;
  double speed = 1.0;
  guint timer_id;
  bool playing;
  GtkWidget *play_btn;
};
static std::vector<std::unique_ptr<SliderAnimData>> slider_anims;

struct SettingsUI {
  GtkWidget *x_min_spin;
  GtkWidget *x_max_spin;
  GtkWidget *y_min_spin;
  GtkWidget *y_max_spin;
} settings_ui;

static bool updating_ui = false;

static gboolean slider_anim_tick(gpointer data) {
  SliderAnimData *anim = static_cast<SliderAnimData *>(data);
  if (!anim->playing)
    return G_SOURCE_REMOVE;
  double val = gtk_range_get_value(anim->range);
  val += anim->step * anim->speed;
  if (val > anim->max_val)
    val = anim->min_val;
  else if (val < anim->min_val)
    val = anim->max_val;
  gtk_range_set_value(anim->range, val);
  return G_SOURCE_CONTINUE;
}

static void on_play_clicked(GtkButton *btn, gpointer data) {
  SliderAnimData *anim = static_cast<SliderAnimData *>(data);
  anim->playing = !anim->playing;
  if (anim->playing) {
    gtk_button_set_icon_name(btn, "media-playback-pause-symbolic");
    anim->timer_id = g_timeout_add(50, slider_anim_tick, anim);
  } else {
    gtk_button_set_icon_name(btn, "media-playback-start-symbolic");
    if (anim->timer_id) {
      g_source_remove(anim->timer_id);
      anim->timer_id = 0;
    }
  }
}

struct EquationData {
  GtkWidget *row_box;
  GtkWidget *editor_area;
  std::unique_ptr<MathEditor> editor;
  GtkWidget *color_btn;
  GtkWidget *delete_btn;
  GtkWidget *slider_box; // sliders for params
  GtkWidget *result_label;
  GtkWidget *symbolic_result_area;
  std::unique_ptr<expr> ast;
  std::unique_ptr<expr> result_ast; // symbolic result
  std::string original_expression;  // for labels
  std::string error;
  int color_idx;
  bool visible = true;
  double theta_min = 0, theta_max = 2 * M_PI;
  GtkWidget *theta_box;
  GtkWidget *theta_min_entry;
  GtkWidget *theta_max_entry;
  GtkWidget *slider_min_entry;
  GtkWidget *slider_max_entry;
  GtkWidget *slider_speed_entry;
  GtkWidget *slider_step_entry;
  GtkWidget *slider_error_label;
  double slider_min_val = -10, slider_max_val = 10;
  double slider_speed = 1.0;
  double slider_step = 0.1;
  GtkWidget *main_area; // main graph ref
};

static void add_equation_row(GtkWidget *vbox, GtkWidget *drawing_area);
static void sync_sliders(EquationData *eq, GtkWidget *drawing_area);

static std::vector<std::unique_ptr<EquationData>> equations;

static void on_draw(GtkDrawingArea *area, cairo_t *cr, int width, int height,
                    gpointer data) {
  (void)area;
  (void)data;
  plotter.render(cr, width, height, eval_engine);
}

static void sync_settings_ui(GtkWidget *area) {
  if (updating_ui)
    return;
  updating_ui = true;

  int w = gtk_widget_get_width(area);
  int h = gtk_widget_get_height(area);

  x_axis_min = plotter.center_x - (w / 2.0) / plotter.zoom_x;
  x_axis_max = plotter.center_x + (w / 2.0) / plotter.zoom_x;
  y_axis_min = plotter.center_y - (h / 2.0) / plotter.zoom_y;
  y_axis_max = plotter.center_y + (h / 2.0) / plotter.zoom_y;

  if (settings_ui.x_min_spin)
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.x_min_spin),
                              x_axis_min);
  if (settings_ui.x_max_spin)
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.x_max_spin),
                              x_axis_max);
  if (settings_ui.y_min_spin)
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.y_min_spin),
                              y_axis_min);
  if (settings_ui.y_max_spin)
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.y_max_spin),
                              y_axis_max);

  updating_ui = false;
}

static void update_plotter(GtkWidget *area) {
  eval_engine.ctx.use_radians = plotter.settings.use_radians;

  const double COLORS[][3] = {
      {0.96, 0.62, 0.04}, // orange
      {0.38, 0.65, 0.98}, // blue
      {0.20, 0.83, 0.60}, // green
      {0.97, 0.44, 0.44}, // red
      {0.75, 0.52, 0.99}  // purple
  };

  // pass 1 global assign
  eval_engine.ctx.vars.clear();
  eval_engine.ctx.funcs.clear();
  eval_engine.load_constants();
  eval_engine.load_builtins();

  for (const auto &eq : equations) {
    std::string s = eq->editor->get_expression();
    if (s.empty())
      continue;

    size_t eq_pos = s.find('=');
    if (eq_pos != std::string::npos) {
      std::string lhs = s.substr(0, eq_pos);
      std::string rhs = s.substr(eq_pos + 1);
      // trim lhs
      lhs.erase(0, lhs.find_first_not_of(" \t"));
      lhs.erase(lhs.find_last_not_of(" \t") + 1);
      rhs.erase(0, rhs.find_first_not_of(" \t"));
      rhs.erase(rhs.find_last_not_of(" \t") + 1);

      // check simple assign
      bool is_assignment = true;
      if (lhs.empty())
        is_assignment = false;
      for (char c : lhs)
        if (!std::isalnum(c) && c != '_' && c != '(' && c != ')' && c != ',') {
          is_assignment = false;
          break;
        }

      // reserve r x y theta
      std::string lhs_check = lhs;
      lhs_check.erase(
          std::remove_if(lhs_check.begin(), lhs_check.end(),
                         [](char c) { return std::isspace(c) || c == '\\'; }),
          lhs_check.end());

      if (lhs_check == "r" || lhs == "y" || lhs == "x" ||
          lhs_check == "\u03b8" || lhs_check == "theta")
        is_assignment = false;

      if (is_assignment) {
        size_t paren_open = lhs.find('(');
        if (paren_open != std::string::npos) {
          // fn assign f(x) = ...
          std::string fname = lhs.substr(0, paren_open);
          std::string params_str = lhs.substr(paren_open + 1);
          if (!params_str.empty() && params_str.back() == ')')
            params_str.pop_back();

          std::vector<std::string> params;
          size_t start = 0, comma;
          while ((comma = params_str.find(',', start)) != std::string::npos) {
            std::string p = params_str.substr(start, comma - start);
            p.erase(0, p.find_first_not_of(" \t"));
            p.erase(p.find_last_not_of(" \t") + 1);
            if (!p.empty())
              params.push_back(p);
            start = comma + 1;
          }
          std::string p = params_str.substr(start);
          p.erase(0, p.find_first_not_of(" \t"));
          p.erase(p.find_last_not_of(" \t") + 1);
          if (!p.empty())
            params.push_back(p);

          try {
            auto tokens = tokenize(rhs);
            parser pr(tokens);
            auto body = pr.parse_expr();
            if (body) {
              eval_engine.ctx.funcs[fname] = {params, std::move(body)};
            }
          } catch (...) {
          }
        } else {
          // var assign a = 5
          try {
            auto tokens = tokenize(rhs);
            parser pr(tokens);
            auto node = pr.parse_expr();
            if (node) {
              double val = node->eval(eval_engine.ctx);
              eval_engine.ctx.vars[lhs] = val;
            }
          } catch (...) {
          }
        }
      }
    }
  }

  plotter.expressions.clear();
  for (const auto &eq : equations) {
    std::string s = eq->editor->get_expression();
    if (s.empty())
      continue;

    // smart strip y = ...
    size_t eq_pos = s.find('=');
    std::string clean_s = s;
    bool is_plot = true;
    bool is_implicit = false;

    if (eq_pos != std::string::npos) {
      std::string lhs = s.substr(0, eq_pos);
      std::string rhs = s.substr(eq_pos + 1);

      // trim lhs
      lhs.erase(0, lhs.find_first_not_of(" \t"));
      lhs.erase(lhs.find_last_not_of(" \t") + 1);

      if (lhs == "y" || lhs == "f(x)" || lhs == "g(x)" || lhs == "h(x)") {
        clean_s = rhs;
        if (clean_s.find('y') != std::string::npos) {
          clean_s = "(y)-(" + rhs + ")";
          is_implicit = true;
        } else {
          is_implicit = false;
        }
      } else if (lhs == "r" || lhs == "r(\\theta)" || lhs == "r(\u03b8)") {
        // polar detected
        is_plot = true;
      } else if (lhs != "x" && lhs.find('(') == std::string::npos &&
                 lhs.find_first_of("+-*/") == std::string::npos) {
        // global assign
        is_plot = false;
      } else {
        // generic a + bx = 0
      }
    } else {
      clean_s = s;
      is_implicit = false;
    }

    bool is_polar = false;
    std::string lhs_trimmed = "";
    if (eq_pos != std::string::npos) {
      lhs_trimmed = s.substr(0, eq_pos);
      // remove space and slash
      lhs_trimmed.erase(
          std::remove_if(lhs_trimmed.begin(), lhs_trimmed.end(),
                         [](char c) { return std::isspace(c) || c == '\\'; }),
          lhs_trimmed.end());
    }

    if (lhs_trimmed == "r" || lhs_trimmed == "r(theta)" ||
        lhs_trimmed == "r(\u03b8)") {
      is_polar = true;
      is_implicit = false;
      clean_s = s.substr(eq_pos + 1);
    }
    auto eval_bound = [&](GtkWidget *entry, double backup_val,
                          double default_val) {
      if (!entry)
        return backup_val;
      const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
      std::string text_s(text);
      text_s.erase(0, text_s.find_first_not_of(" \t"));
      text_s.erase(text_s.find_last_not_of(" \t") + 1);
      if (text_s.empty())
        return default_val;

      try {
        auto ts = tokenize(text_s);
        if (ts.empty() || ts[0].type == tokentype::eof)
          return default_val;
        parser p(ts);
        auto node = p.parse_expr();
        if (node)
          return node->eval(eval_engine.ctx);
      } catch (...) {
      }
      return backup_val;
    };

    if (is_polar) {
      eq->theta_min = eval_bound(eq->theta_min_entry, eq->theta_min, 0.0);
      eq->theta_max = eval_bound(eq->theta_max_entry, eq->theta_max, 2 * M_PI);
    } else if (!is_plot && eq->slider_box) {
      eq->slider_min_val =
          eval_bound(eq->slider_min_entry, eq->slider_min_val, -10.0);
      eq->slider_max_val =
          eval_bound(eq->slider_max_entry, eq->slider_max_val, 10.0);
      eq->slider_speed =
          eval_bound(eq->slider_speed_entry, eq->slider_speed, 1.0);
      eq->slider_step = eval_bound(eq->slider_step_entry, eq->slider_step, 0.1);

      // check slider bound error
      if (eq->slider_error_label) {
        if (eq->slider_min_val > eq->slider_max_val) {
          gtk_label_set_text(GTK_LABEL(eq->slider_error_label),
                             "Error: Min bound > Max bound");
          gtk_widget_set_visible(eq->slider_error_label, true);
        } else {
          gtk_widget_set_visible(eq->slider_error_label, false);
        }
      }

      GtkWidget *row = gtk_widget_get_first_child(eq->slider_box);
      if (row) {
        GtkWidget *slider = nullptr;
        GtkWidget *c = gtk_widget_get_first_child(row);
        while (c) {
          if (GTK_IS_SCALE(c)) {
            slider = c;
            break;
          }
          c = gtk_widget_get_next_sibling(c);
        }
        if (slider) {
          GtkAdjustment *adj = gtk_range_get_adjustment(GTK_RANGE(slider));
          if (std::abs(gtk_adjustment_get_lower(adj) - eq->slider_min_val) >
              1e-9)
            gtk_adjustment_set_lower(adj, eq->slider_min_val);
          if (std::abs(gtk_adjustment_get_upper(adj) - eq->slider_max_val) >
              1e-9)
            gtk_adjustment_set_upper(adj, eq->slider_max_val);

          SliderAnimData *anim = static_cast<SliderAnimData *>(
              g_object_get_data(G_OBJECT(slider), "anim"));
          if (anim) {
            anim->min_val = eq->slider_min_val;
            anim->max_val = eq->slider_max_val;
            anim->speed = eq->slider_speed;
            anim->step = eq->slider_step;
          }
        }
      }
    }
    if (eq->theta_box)
      gtk_widget_set_visible(eq->theta_box, is_polar);

    if (is_plot) {
      try {
        auto tokens = tokenize(clean_s);
        parser p(tokens);
        auto ast = p.parse_expr();
        if (ast) {
          try {
            ast = ast->simplify();
          } catch (...) {
            ast = nullptr;
          }
          if (ast) {
            auto &c = COLORS[eq->color_idx % 5];
            plotter.expressions.push_back({std::move(ast), c[0], c[1], c[2],
                                           eq->visible, is_implicit, is_polar,
                                           eq->theta_min, eq->theta_max, s});

            // result number display
            std::set<std::string> vars;
            plotter.expressions.back().ast->collect_variables(vars);
            // if no var it is constant op
            if (vars.find("x") == vars.end() && vars.find("y") == vars.end() &&
                vars.find("theta") == vars.end() && !is_polar) {
              plotter.expressions.back().visible = false;
              try {
                double res =
                    plotter.expressions.back().ast->eval(eval_engine.ctx);
                char buf[64];
                if (std::abs(res - std::round(res)) < 1e-9)
                  snprintf(buf, sizeof(buf), "= %d", (int)std::round(res));
                else
                  snprintf(buf, sizeof(buf), "= %.6g", res);
                gtk_label_set_text(GTK_LABEL(eq->result_label), buf);
                gtk_widget_set_visible(eq->result_label, true);
              } catch (...) {
                gtk_widget_set_visible(eq->result_label, false);
              }
            } else {
              gtk_widget_set_visible(eq->result_label, false);
            }

            // symbolic result for deriv int etc
            if (ast) {
              try {
                // see if we should show symbolic
                bool show_symbolic = false;
                if (dynamic_cast<const deriv_node *>(ast.get()) ||
                    dynamic_cast<const integral *>(ast.get()) ||
                    dynamic_cast<const func_call *>(ast.get())) {
                  show_symbolic = true;
                }

                if (show_symbolic) {
                  auto sym_res = ast->expand(eval_engine.ctx)->simplify();
                  // show if diff from input or simplified a lot
                  if (sym_res && !sym_res->equals(*ast)) {
                    eq->result_ast = std::move(sym_res);
                    gtk_widget_set_visible(eq->symbolic_result_area, true);
                    gtk_widget_queue_draw(eq->symbolic_result_area);
                  } else {
                    eq->result_ast = nullptr;
                    gtk_widget_set_visible(eq->symbolic_result_area, false);
                  }
                } else {
                  eq->result_ast = nullptr;
                  gtk_widget_set_visible(eq->symbolic_result_area, false);
                }
              } catch (...) {
                eq->result_ast = nullptr;
                gtk_widget_set_visible(eq->symbolic_result_area, false);
              }
            } else {
              eq->result_ast = nullptr;
              gtk_widget_set_visible(eq->symbolic_result_area, false);
            }
          }
        }
      } catch (...) {
        gtk_widget_set_visible(eq->result_label, false);
        gtk_widget_set_visible(eq->symbolic_result_area, false);
      }
    } else {
      gtk_widget_set_visible(eq->result_label, false);
    }

    // result display for assign a = sin(x)
    if (!is_plot && eq->ast) {
      try {
        double res = eq->ast->eval(eval_engine.ctx);
        char buf[64];
        if (std::abs(res - std::round(res)) < 1e-9)
          snprintf(buf, sizeof(buf), "= %d", (int)std::round(res));
        else
          snprintf(buf, sizeof(buf), "= %.6g", res);
        gtk_label_set_text(GTK_LABEL(eq->result_label), buf);
        gtk_widget_set_visible(eq->result_label, true);
      } catch (...) {
        gtk_widget_set_visible(eq->result_label, false);
      }
    }
  }
  if (area)
    gtk_widget_queue_draw(area);
}

static void trigger_equation_update(EquationData *eq_data) {
  std::string text = eq_data->editor->get_expression();

  try {
    if (text.empty() || text == " ") {
      eq_data->ast = nullptr;
      eq_data->error = "";
    } else {
      auto tokens = tokenize(text);
      parser p(tokens);
      eq_data->ast = p.parse_expr();
      if (eq_data->ast) {
        try {
          eq_data->ast = eq_data->ast->simplify();
        } catch (...) {
          eq_data->ast = nullptr;
          eq_data->error = "Incomplete expression";
        }
      }
      eq_data->original_expression = text;
      eq_data->error = "";

      // update sliders for eq
      GtkWidget *area = static_cast<GtkWidget *>(
          g_object_get_data(G_OBJECT(eq_data->editor_area), "drawing-area"));
      sync_sliders(eq_data, area);
    }
  } catch (const std::exception &e) {
    eq_data->ast = nullptr;
    eq_data->error = e.what();
  }

  // refresh plotter
  GtkWidget *area = static_cast<GtkWidget *>(
      g_object_get_data(G_OBJECT(eq_data->editor_area), "drawing-area"));
  update_plotter(area);
}

static void on_editor_draw(GtkDrawingArea *area, cairo_t *cr, int width,
                           int height, gpointer data) {
  (void)area;
  (void)width;
  EquationData *eq = static_cast<EquationData *>(data);
  // draw bg transparent
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);

  cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
  eq->editor->draw(cr, 10, height / 2.0 + 6, 20.0);
}

static void on_symbolic_draw(GtkDrawingArea *area, cairo_t *cr, int width,
                             int height, gpointer data) {
  (void)area;
  (void)width;
  EquationData *eq = static_cast<EquationData *>(data);
  if (eq->result_ast) {
    // opacity gray color
    cairo_set_source_rgba(cr, 0.53, 0.53, 0.53, 1.0);
    MathRenderer renderer(cr, 16.0);
    renderer.render(*eq->result_ast, 30, height / 2.0 + 4);
  }
}

static gboolean on_editor_key(GtkEventControllerKey *controller, guint keyval,
                              guint keycode, GdkModifierType state,
                              gpointer data) {
  (void)controller;
  (void)keycode;
  (void)state;
  EquationData *eq = static_cast<EquationData *>(data);
  MathEditor *editor = eq->editor.get();

  if (keyval == GDK_KEY_Left) {
    editor->move_left();
  } else if (keyval == GDK_KEY_Right) {
    editor->move_right();
  } else if (keyval == GDK_KEY_BackSpace) {
    editor->backspace();
  } else if (keyval == GDK_KEY_Delete) {
    editor->delete_forward();
  } else if (keyval == GDK_KEY_slash) {
    editor->insert_fraction();
  } else if (keyval == GDK_KEY_asciicircum) {
    editor->insert_power();
  } else if (keyval == GDK_KEY_underscore) {
    // jump into lower bound of int
    if (editor->cursor_index > 0) {
      auto *prev = dynamic_cast<MathIntegral *>(
          editor->active_box->nodes[editor->cursor_index - 1].get());
      if (prev) {
        editor->active_box = prev->lower.get();
        editor->cursor_index = 0;
      }
    }
  } else if (keyval == GDK_KEY_Return) {
    // make new line maybe
  } else {
    // print ascii chars
    if (keyval >= 32 && keyval <= 126) {
      std::string s(1, (char)keyval);
      editor->insert_char(s);
    }
  }

  gtk_widget_queue_draw(eq->editor_area);
  trigger_equation_update(eq);
  return TRUE;
}

static void on_editor_click(GtkGestureClick *gesture, int n_press, double x,
                            double y, gpointer data) {
  (void)gesture;
  (void)n_press;
  EquationData *eq = static_cast<EquationData *>(data);
  gtk_widget_grab_focus(eq->editor_area);
  eq->editor->handle_click(x, y);
  gtk_widget_queue_draw(eq->editor_area);
}

// end of click navigation

static void on_add_clicked(GtkButton *btn, gpointer data) {
  (void)data;
  GtkWidget *vbox = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "vbox"));
  GtkWidget *area = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "area"));
  add_equation_row(vbox, area);
}

static void on_slider_changed(GtkRange *range, gpointer data) {
  (void)data;
  const char *var_name =
      static_cast<const char *>(g_object_get_data(G_OBJECT(range), "var_name"));
  double value = gtk_range_get_value(range);
  eval_engine.ctx.vars[var_name] = value;

  GtkWidget *val_label =
      GTK_WIDGET(g_object_get_data(G_OBJECT(range), "val-label"));
  // update value label
  snprintf(buf, sizeof(buf), "%.2f", value);
  gtk_label_set_text(GTK_LABEL(val_label), buf);
}

EquationData *eq =
    static_cast<EquationData *>(g_object_get_data(G_OBJECT(range), "eq_data"));
if (eq) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%s = %.2f", var_name, value);
  eq->editor->set_expression(buf);
  gtk_widget_queue_draw(eq->editor_area);

  // update ast for plot change
  auto tokens = tokenize(buf);
  parser p(tokens);
  try {
    eq->ast = p.parse_expr();
  } catch (...) {
  }
}

GtkWidget *area =
    GTK_WIDGET(g_object_get_data(G_OBJECT(range), "drawing-area"));
update_plotter(area);
}

static void on_delete_clicked(GtkButton *btn, gpointer data) {
  EquationData *eq_data = static_cast<EquationData *>(data);
  for (auto it = equations.begin(); it != equations.end(); ++it) {
    if (it->get() == eq_data) {
      gtk_box_remove(GTK_BOX(equations_vbox), eq_data->row_box);
      equations.erase(it);
      break;
    }
  }
  GtkWidget *area =
      GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "drawing-area"));
  update_plotter(area);
}

static void on_slider_bound_changed(GtkEditable * /*ed*/, gpointer data) {
  GtkWidget *area = GTK_WIDGET(data);
  // redraw main for context update
  gtk_widget_queue_draw(area);
}

// use shared on slider bound changed

static void sync_sliders(EquationData *eq, GtkWidget *drawing_area) {
  // clear existing sliders in eq box
  GtkWidget *child = gtk_widget_get_first_child(eq->slider_box);
  while (child) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_box_remove(GTK_BOX(eq->slider_box), child);
    child = next;
  }

  std::string text = eq->editor->get_expression();
  size_t eq_pos = text.find('=');
  if (eq_pos == std::string::npos)
    return;

  std::string lhs = text.substr(0, eq_pos);
  std::string rhs = text.substr(eq_pos + 1);
  lhs.erase(0, lhs.find_first_not_of(" \t"));
  lhs.erase(lhs.find_last_not_of(" \t") + 1);

  // simple assign check one word lhs
  bool is_assignment = !lhs.empty();
  for (char c : lhs)
    if (!std::isalnum(c) && c != '_' && c != '\\') {
      is_assignment = false;
      break;
    }

  // remove spaces and slash for compare
  std::string lhs_check = lhs;
  lhs_check.erase(
      std::remove_if(lhs_check.begin(), lhs_check.end(),
                     [](char c) { return std::isspace(c) || c == '\\'; }),
      lhs_check.end());

  if (lhs_check == "y" || lhs_check == "x" || lhs_check == "r" ||
      lhs_check == "theta" || lhs_check == "\u03b8" ||
      lhs.find('(') != std::string::npos)
    is_assignment = false;

  if (!is_assignment)
    return;

  const std::string &v = lhs;

  if (eval_engine.ctx.vars.find(v) == eval_engine.ctx.vars.end()) {
    eval_engine.ctx.vars[v] = 1.0;
  }

  double cur_val = eval_engine.ctx.vars[v];

  // slider row with play button
  GtkWidget *slider_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_margin_start(slider_hbox, 4);
  gtk_widget_set_margin_end(slider_hbox, 4);

  // play btn
  auto anim = std::make_unique<SliderAnimData>();
  GtkWidget *play_btn =
      gtk_button_new_from_icon_name("media-playback-start-symbolic");
  gtk_widget_add_css_class(play_btn, "flat");
  gtk_widget_add_css_class(play_btn, "slider-play-btn");
  gtk_widget_set_size_request(play_btn, 24, 24);

  // min bound entry
  GtkWidget *min_entry = gtk_entry_new();
  eq->slider_min_entry = min_entry;
  char min_buf[32];
  snprintf(min_buf, sizeof(min_buf), "%.2f", eq->slider_min_val);
  gtk_editable_set_text(GTK_EDITABLE(min_entry), min_buf);
  gtk_widget_set_size_request(min_entry, 45, -1);
  gtk_widget_add_css_class(min_entry, "slider-bound-entry");

  // slider
  GtkWidget *slider = gtk_scale_new_with_range(
      GTK_ORIENTATION_HORIZONTAL, eq->slider_min_val, eq->slider_max_val, 0.1);
  gtk_range_set_value(GTK_RANGE(slider), cur_val);
  gtk_widget_set_hexpand(slider, TRUE);
  gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);

  // max bound entry
  GtkWidget *max_entry = gtk_entry_new();
  eq->slider_max_entry = max_entry;
  char max_buf[32];
  snprintf(max_buf, sizeof(max_buf), "%.2f", eq->slider_max_val);
  gtk_editable_set_text(GTK_EDITABLE(max_entry), max_buf);
  gtk_widget_set_size_request(max_entry, 45, -1);
  gtk_widget_add_css_class(max_entry, "slider-bound-entry");

  // value label
  char val_buf[32];
  snprintf(val_buf, sizeof(val_buf), "%.2f", cur_val);
  GtkWidget *val_label = gtk_label_new(val_buf);
  gtk_label_set_xalign(GTK_LABEL(val_label), 1.0);
  gtk_widget_add_css_class(val_label, "slider-value");

  g_object_set_data_full(G_OBJECT(slider), "var_name", g_strdup(v.c_str()),
                         g_free);
  g_object_set_data(G_OBJECT(slider), "drawing-area", drawing_area);
  g_object_set_data(G_OBJECT(slider), "eq_data", eq);
  g_object_set_data(G_OBJECT(slider), "val-label", val_label);
  g_object_set_data(G_OBJECT(slider), "anim", anim.get());

  g_signal_connect(slider, "value-changed", G_CALLBACK(on_slider_changed),
                   NULL);

  // connect entries
  g_signal_connect(min_entry, "changed", G_CALLBACK(on_slider_bound_changed),
                   drawing_area);
  g_signal_connect(max_entry, "changed", G_CALLBACK(on_slider_bound_changed),
                   drawing_area);

  // speed and step row
  GtkWidget *params_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  gtk_widget_set_margin_start(params_hbox, 32);

  GtkWidget *speed_label = gtk_label_new("Speed:");
  gtk_widget_add_css_class(speed_label, "slider-param-label");
  GtkWidget *speed_entry = gtk_entry_new();
  eq->slider_speed_entry = speed_entry;
  char speed_buf[16];
  snprintf(speed_buf, sizeof(speed_buf), "%.1f", eq->slider_speed);
  gtk_editable_set_text(GTK_EDITABLE(speed_entry), speed_buf);
  gtk_widget_set_size_request(speed_entry, 35, -1);

  GtkWidget *step_label = gtk_label_new("Step:");
  gtk_widget_add_css_class(step_label, "slider-param-label");
  GtkWidget *step_entry = gtk_entry_new();
  eq->slider_step_entry = step_entry;
  char step_buf[16];
  snprintf(step_buf, sizeof(step_buf), "%.2f", eq->slider_step);
  gtk_editable_set_text(GTK_EDITABLE(step_entry), step_buf);
  gtk_widget_set_size_request(step_entry, 45, -1);

  g_signal_connect(speed_entry, "changed", G_CALLBACK(on_slider_bound_changed),
                   drawing_area);
  g_signal_connect(step_entry, "changed", G_CALLBACK(on_slider_bound_changed),
                   drawing_area);

  gtk_box_append(GTK_BOX(params_hbox), speed_label);
  gtk_box_append(GTK_BOX(params_hbox), speed_entry);
  gtk_box_append(GTK_BOX(params_hbox), step_label);
  gtk_box_append(GTK_BOX(params_hbox), step_entry);

  // err label
  GtkWidget *err_label = gtk_label_new("");
  eq->slider_error_label = err_label;
  gtk_widget_add_css_class(err_label, "slider-error-text");
  gtk_widget_set_visible(err_label, false);
  gtk_widget_set_margin_start(err_label, 32);

  // anim data
  anim->range = GTK_RANGE(slider);
  anim->min_val = eq->slider_min_val;
  anim->max_val = eq->slider_max_val;
  anim->step = eq->slider_step;
  anim->speed = eq->slider_speed;
  anim->timer_id = 0;
  anim->playing = false;
  anim->play_btn = play_btn;

  SliderAnimData *anim_ptr = anim.get();
  slider_anims.push_back(std::move(anim));

  g_signal_connect(play_btn, "clicked", G_CALLBACK(on_play_clicked), anim_ptr);

  gtk_box_append(GTK_BOX(slider_hbox), play_btn);
  gtk_box_append(GTK_BOX(slider_hbox), min_entry);
  gtk_box_append(GTK_BOX(slider_hbox), slider);
  gtk_box_append(GTK_BOX(slider_hbox), max_entry);

  gtk_box_append(GTK_BOX(eq->slider_box), slider_hbox);
  gtk_box_append(GTK_BOX(eq->slider_box), params_hbox);
  gtk_box_append(GTK_BOX(eq->slider_box), err_label);
}

static void on_color_indicator_draw(GtkDrawingArea * /*var*/, cairo_t *cr,
                                    int width, int height, gpointer data) {
  EquationData *eq = (EquationData *)data;
  double r = 0, g = 0, b = 0;
  const double COLORS[][3] = {{0.96, 0.62, 0.04},
                              {0.38, 0.65, 0.98},
                              {0.20, 0.83, 0.60},
                              {0.97, 0.44, 0.44},
                              {0.75, 0.52, 0.99}};
  int idx = eq->color_idx % 5;
  r = COLORS[idx][0];
  g = COLORS[idx][1];
  b = COLORS[idx][2];

  double cx = width / 2.0;
  double cy = height / 2.0;
  double radius = std::min(width, height) / 2.0 - 2;

  cairo_arc(cr, cx, cy, radius, 0, 2 * M_PI);
  if (eq->visible) {
    cairo_set_source_rgb(cr, r, g, b);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.2);
    cairo_set_line_width(cr, 1.5);
    cairo_stroke(cr);
  } else {
    cairo_set_source_rgba(cr, r, g, b, 0.3);
    cairo_fill_preserve(cr);
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.5);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
  }
}

static void on_color_pressed(GtkGestureClick *gesture, int n_press, double x,
                             double y, gpointer data) {
  (void)n_press;
  (void)x;
  (void)y;
  EquationData *eq = (EquationData *)data;
  guint button =
      gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

  if (button == 1) { // left click
    eq->visible = !eq->visible;
    gtk_widget_queue_draw(eq->color_btn);
    update_plotter(eq->main_area);
    sync_settings_ui(eq->main_area);
  } else if (button == 3) { // right click
    GtkWidget *popover = gtk_popover_new();
    gtk_widget_set_parent(popover, eq->color_btn);
    gtk_widget_add_css_class(popover, "settings-popover");

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_margin_start(box, 10);
    gtk_widget_set_margin_end(box, 10);
    gtk_widget_set_margin_top(box, 10);
    gtk_widget_set_margin_bottom(box, 10);

    struct ColorChoice {
      EquationData *eq;
      int idx;
      GtkWidget *pop;
    };

    for (int i = 0; i < 5; ++i) {
      GtkWidget *cbtn = gtk_button_new();
      gtk_widget_set_size_request(cbtn, 24, 24);
      gtk_widget_add_css_class(cbtn, "color-indicator");
      char cclass[32];
      snprintf(cclass, sizeof(cclass), "color-%d", i);
      gtk_widget_add_css_class(cbtn, cclass);

      ColorChoice *choice = new ColorChoice{eq, i, popover};

      g_signal_connect_data(
          cbtn, "clicked",
          (GCallback) +
              [](GtkButton * /*b*/, gpointer d) {
                ColorChoice *c = (ColorChoice *)d;
                char old_c[32], new_c[32];
                snprintf(old_c, sizeof(old_c), "color-%d",
                         c->eq->color_idx % 5);
                snprintf(new_c, sizeof(new_c), "color-%d", c->idx % 5);
                c->eq->color_idx = c->idx;
                gtk_widget_queue_draw(c->eq->color_btn);
                update_plotter(c->eq->main_area);
                gtk_popover_popdown(GTK_POPOVER(c->pop));
              },
          choice,
          (GClosureNotify)[](gpointer d, GClosure *) {
            delete (ColorChoice *)d;
          },
          (GConnectFlags)0);

      gtk_box_append(GTK_BOX(box), cbtn);
    }

    gtk_popover_set_child(GTK_POPOVER(popover), box);
    gtk_popover_popup(GTK_POPOVER(popover));
  }
}

static void add_equation_row(GtkWidget *vbox, GtkWidget *drawing_area) {
  static int color_counter = 0;

  auto eq_ptr = std::make_unique<EquationData>();
  EquationData *eq = eq_ptr.get();
  eq->color_idx = color_counter++;
  eq->result_ast = nullptr;
  eq->symbolic_result_area = nullptr;
  eq->main_area = drawing_area;

  eq->row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_add_css_class(eq->row_box, "equation-row");
  gtk_widget_set_margin_bottom(eq->row_box, 10);

  GtkWidget *top_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

  // color indicator draw area
  eq->color_btn = gtk_drawing_area_new();
  gtk_widget_set_size_request(eq->color_btn, 24, 24);
  gtk_widget_set_valign(eq->color_btn, GTK_ALIGN_CENTER);
  gtk_widget_set_halign(eq->color_btn, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start(eq->color_btn, 4);
  gtk_widget_set_margin_end(eq->color_btn, 4);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(eq->color_btn),
                                 on_color_indicator_draw, eq, NULL);

  GtkGesture *color_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(color_click),
                                0); // all buttons
  g_signal_connect(color_click, "pressed", G_CALLBACK(on_color_pressed), eq);
  gtk_widget_add_controller(eq->color_btn, GTK_EVENT_CONTROLLER(color_click));

  eq->editor = std::make_unique<MathEditor>();
  eq->editor_area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(eq->editor_area, TRUE);
  gtk_widget_set_size_request(eq->editor_area, -1, 50); // 50px high
  gtk_widget_add_css_class(eq->editor_area, "equation-editor");
  gtk_widget_set_focusable(eq->editor_area, TRUE);

  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(eq->editor_area),
                                 on_editor_draw, eq, NULL);

  GtkEventController *key_ctrl = gtk_event_controller_key_new();
  g_signal_connect(key_ctrl, "key-pressed", G_CALLBACK(on_editor_key), eq);
  gtk_widget_add_controller(eq->editor_area, key_ctrl);

  GtkGesture *click = gtk_gesture_click_new();
  g_signal_connect(click, "pressed", G_CALLBACK(on_editor_click), eq);
  gtk_widget_add_controller(eq->editor_area, GTK_EVENT_CONTROLLER(click));

  eq->delete_btn = gtk_button_new_from_icon_name("window-close-symbolic");
  gtk_widget_add_css_class(eq->delete_btn, "delete-btn");

  gtk_box_append(GTK_BOX(top_hbox), eq->color_btn);
  gtk_box_append(GTK_BOX(top_hbox), eq->editor_area);
  gtk_box_append(GTK_BOX(top_hbox), eq->delete_btn);

  gtk_box_append(GTK_BOX(eq->row_box), top_hbox);

  eq->result_label = gtk_label_new("");
  gtk_widget_add_css_class(eq->result_label, "result-label");
  gtk_widget_set_halign(eq->result_label, GTK_ALIGN_START);
  gtk_widget_set_margin_start(eq->result_label, 30);
  gtk_box_append(GTK_BOX(eq->row_box), eq->result_label);

  eq->symbolic_result_area = gtk_drawing_area_new();
  gtk_widget_set_size_request(eq->symbolic_result_area, -1, 30);
  gtk_widget_set_halign(eq->symbolic_result_area, GTK_ALIGN_FILL);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(eq->symbolic_result_area),
                                 on_symbolic_draw, eq, NULL);
  gtk_widget_set_visible(eq->symbolic_result_area, false);
  gtk_box_append(GTK_BOX(eq->row_box), eq->symbolic_result_area);

  eq->slider_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
  gtk_box_append(GTK_BOX(eq->row_box), eq->slider_box);

  // theta bounds row polar plots
  eq->theta_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_margin_start(eq->theta_box, 34);
  gtk_widget_set_visible(eq->theta_box, false);

  eq->theta_min_entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(eq->theta_min_entry), "0");
  gtk_widget_set_size_request(eq->theta_min_entry, 60, -1);
  gtk_box_append(GTK_BOX(eq->theta_box), eq->theta_min_entry);

  GtkWidget *mid_label = gtk_label_new(" \u2264 \u03b8 \u2264 ");
  gtk_box_append(GTK_BOX(eq->theta_box), mid_label);

  eq->theta_max_entry = gtk_entry_new();
  gtk_editable_set_text(GTK_EDITABLE(eq->theta_max_entry), "2pi");
  gtk_widget_set_size_request(eq->theta_max_entry, 60, -1);
  gtk_box_append(GTK_BOX(eq->theta_box), eq->theta_max_entry);

  g_signal_connect(eq->theta_min_entry, "changed",
                   G_CALLBACK(+[](GtkEditable *, gpointer d) {
                     EquationData *eq = (EquationData *)d;
                     update_plotter(eq->main_area);
                   }),
                   eq);

  g_signal_connect(eq->theta_max_entry, "changed",
                   G_CALLBACK(+[](GtkEditable *, gpointer d) {
                     EquationData *eq = (EquationData *)d;
                     update_plotter(eq->main_area);
                   }),
                   eq);

  gtk_box_append(GTK_BOX(eq->row_box), eq->theta_box);

  g_object_set_data(G_OBJECT(eq->editor_area), "drawing-area", drawing_area);
  g_object_set_data(G_OBJECT(eq->editor_area), "eq_data", eq);
  g_object_set_data(G_OBJECT(eq->editor_area), "vbox", vbox);
  g_object_set_data(G_OBJECT(eq->editor_area), "area", drawing_area);

  g_object_set_data(G_OBJECT(eq->delete_btn), "drawing-area", drawing_area);
  g_signal_connect(eq->delete_btn, "clicked", G_CALLBACK(on_delete_clicked),
                   eq);

  gtk_box_append(GTK_BOX(vbox), eq->row_box);
  gtk_widget_grab_focus(eq->editor_area);

  equations.push_back(std::move(eq_ptr));
}

// interact logic

static void on_drag_begin(GtkGestureDrag *gesture, double x, double y,
                          gpointer data) {
  (void)gesture;
  (void)x;
  (void)y;
  (void)data;
  drag_start_center_x = plotter.center_x;
  drag_start_center_y = plotter.center_y;
  dragging = true;
}

static void on_drag_update(GtkGestureDrag *gesture, double x, double y,
                           gpointer data) {
  (void)gesture;
  if (plotter.settings.lock_viewport)
    return;
  GtkWidget *area = GTK_WIDGET(data);
  // xy offsets from drag start
  plotter.center_x = drag_start_center_x - x / plotter.zoom_x;
  plotter.center_y = drag_start_center_y + y / plotter.zoom_y;
  sync_settings_ui(area);
  gtk_widget_queue_draw(area);
}

static gboolean on_scroll(GtkEventControllerScroll *scroll, double dx,
                          double dy, gpointer data) {
  (void)scroll;
  (void)dx;
  if (plotter.settings.lock_viewport)
    return TRUE;
  GtkWidget *area = GTK_WIDGET(data);

  // mouse pos for zoom cursor
  int w = gtk_widget_get_width(area);
  int h = gtk_widget_get_height(area);

  // cursor pos via scroll ctrl
  double mx = w / 2.0, my = h / 2.0; // center fallback
  GdkEvent *event =
      gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(scroll));
  if (event) {
    double ex, ey;
    if (gdk_event_get_position(event, &ex, &ey)) {
      // surface relative coords
      mx = ex;
      my = ey;
    }
  }

  // mouse to math coords before zoom
  double math_x = (mx - w / 2.0) / plotter.zoom_x + plotter.center_x;
  double math_y = (h / 2.0 - my) / plotter.zoom_y + plotter.center_y;

  // apply zoom
  double factor = (dy < 0) ? 1.1 : 1.0 / 1.1;
  plotter.zoom_x *= factor;
  plotter.zoom_y *= factor;

  // adjust center for cursor math point
  plotter.center_x = math_x - (mx - w / 2.0) / plotter.zoom_x;
  plotter.center_y = math_y + (my - h / 2.0) / plotter.zoom_y;

  sync_settings_ui(area);
  gtk_widget_queue_draw(area);
  return TRUE;
}

static void on_settings_grid_toggled(GtkCheckButton *btn, gpointer data) {
  plotter.settings.show_grid = gtk_check_button_get_active(btn);
  GtkWidget *area = GTK_WIDGET(data);
  gtk_widget_queue_draw(area);
}
static void on_settings_axis_nums_toggled(GtkCheckButton *btn, gpointer data) {
  plotter.settings.show_axis_numbers = gtk_check_button_get_active(btn);
  GtkWidget *area = GTK_WIDGET(data);
  gtk_widget_queue_draw(area);
}
static void on_settings_minor_grid_toggled(GtkCheckButton *btn, gpointer data) {
  plotter.settings.show_minor_gridlines = gtk_check_button_get_active(btn);
  GtkWidget *area = GTK_WIDGET(data);
  gtk_widget_queue_draw(area);
}
static void on_settings_arrows_toggled(GtkCheckButton *btn, gpointer data) {
  plotter.settings.show_arrows = gtk_check_button_get_active(btn);
  GtkWidget *area = GTK_WIDGET(data);
  gtk_widget_queue_draw(area);
}
static void on_settings_lock_viewport_toggled(GtkCheckButton *btn, gpointer) {
  plotter.settings.lock_viewport = gtk_check_button_get_active(btn);
}
static void on_settings_main_axis_toggled(GtkCheckButton *btn, gpointer data) {
  plotter.settings.show_main_axis = gtk_check_button_get_active(btn);
  update_plotter(GTK_WIDGET(data));
}
static void on_settings_lock_aspect_toggled(GtkCheckButton *btn, gpointer) {
  plotter.settings.lock_aspect_ratio = gtk_check_button_get_active(btn);
}
static void on_settings_radians_toggled(GtkCheckButton *btn, gpointer data) {
  plotter.settings.use_radians = gtk_check_button_get_active(btn);
  GtkWidget *area = GTK_WIDGET(data);
  update_plotter(area);
  gtk_widget_queue_draw(area);
}
static void on_x_min_changed(GtkSpinButton *spin, gpointer data) {
  if (updating_ui)
    return;
  x_axis_min = gtk_spin_button_get_value(spin);
  GtkWidget *area = GTK_WIDGET(data);
  int w = gtk_widget_get_width(area);
  double range_x = x_axis_max - x_axis_min;
  if (range_x > 0) {
    plotter.center_x = (x_axis_min + x_axis_max) / 2.0;
    plotter.zoom_x = w / range_x;
    if (plotter.settings.lock_aspect_ratio) {
      plotter.zoom_y = plotter.zoom_x;
      int h = gtk_widget_get_height(area);
      double range_y = h / plotter.zoom_y;
      y_axis_min = plotter.center_y - range_y / 2.0;
      y_axis_max = plotter.center_y + range_y / 2.0;
      updating_ui = true;
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.y_min_spin),
                                y_axis_min);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.y_max_spin),
                                y_axis_max);
      updating_ui = false;
    }
    gtk_widget_queue_draw(area);
  }
}
static void on_x_max_changed(GtkSpinButton *spin, gpointer data) {
  if (updating_ui)
    return;
  x_axis_max = gtk_spin_button_get_value(spin);
  GtkWidget *area = GTK_WIDGET(data);
  int w = gtk_widget_get_width(area);
  double range_x = x_axis_max - x_axis_min;
  if (range_x > 0) {
    plotter.center_x = (x_axis_min + x_axis_max) / 2.0;
    plotter.zoom_x = w / range_x;
    if (plotter.settings.lock_aspect_ratio) {
      plotter.zoom_y = plotter.zoom_x;
      int h = gtk_widget_get_height(area);
      double range_y = h / plotter.zoom_y;
      y_axis_min = plotter.center_y - range_y / 2.0;
      y_axis_max = plotter.center_y + range_y / 2.0;
      updating_ui = true;
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.y_min_spin),
                                y_axis_min);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.y_max_spin),
                                y_axis_max);
      updating_ui = false;
    }
    gtk_widget_queue_draw(area);
  }
}
static void on_y_min_changed(GtkSpinButton *spin, gpointer data) {
  if (updating_ui)
    return;
  y_axis_min = gtk_spin_button_get_value(spin);
  GtkWidget *area = GTK_WIDGET(data);
  int h = gtk_widget_get_height(area);
  double range_y = y_axis_max - y_axis_min;
  if (range_y > 0) {
    plotter.center_y = (y_axis_min + y_axis_max) / 2.0;
    plotter.zoom_y = h / range_y;
    if (plotter.settings.lock_aspect_ratio) {
      plotter.zoom_x = plotter.zoom_y;
      int w = gtk_widget_get_width(area);
      double range_x = w / plotter.zoom_x;
      x_axis_min = plotter.center_x - range_x / 2.0;
      x_axis_max = plotter.center_x + range_x / 2.0;
      updating_ui = true;
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.x_min_spin),
                                x_axis_min);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.x_max_spin),
                                x_axis_max);
      updating_ui = false;
    }
    gtk_widget_queue_draw(area);
  }
}
static void on_y_max_changed(GtkSpinButton *spin, gpointer data) {
  if (updating_ui)
    return;
  y_axis_max = gtk_spin_button_get_value(spin);
  GtkWidget *area = GTK_WIDGET(data);
  int h = gtk_widget_get_height(area);
  double range_y = y_axis_max - y_axis_min;
  if (range_y > 0) {
    plotter.center_y = (y_axis_min + y_axis_max) / 2.0;
    plotter.zoom_y = h / range_y;
    if (plotter.settings.lock_aspect_ratio) {
      plotter.zoom_x = plotter.zoom_y;
      int w = gtk_widget_get_width(area);
      double range_x = w / plotter.zoom_x;
      x_axis_min = plotter.center_x - range_x / 2.0;
      x_axis_max = plotter.center_x + range_x / 2.0;
      updating_ui = true;
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.x_min_spin),
                                x_axis_min);
      gtk_spin_button_set_value(GTK_SPIN_BUTTON(settings_ui.x_max_spin),
                                x_axis_max);
      updating_ui = false;
    }
    gtk_widget_queue_draw(area);
  }
}
static void on_x_step_changed(GtkSpinButton *spin, gpointer) {
  plotter.settings.x_step = gtk_spin_button_get_value(spin);
}
static void on_y_step_changed(GtkSpinButton *spin, gpointer) {
  plotter.settings.y_step = gtk_spin_button_get_value(spin);
}

static void activate(GtkApplication *app, gpointer user_data) {
  (void)user_data;

  // load css
  GtkCssProvider *provider = gtk_css_provider_new();
  gtk_css_provider_load_from_string(
      provider,
      ".sidebar { background-color: #111; border-right: 1px solid #333; }\n"
      ".equation-row { padding: 8px 12px; border-bottom: 1px solid #222; "
      "transition: background 0.2s; }\n"
      ".equation-row:hover { background-color: #1a1a1a; }\n"
      ".color-indicator { border-radius: 999px; border: 2px solid "
      "rgba(255,255,255,0.2); min-width: 26px; min-height: 26px; "
      "margin: 0; padding: 0; box-shadow: 0 2px 4px rgba(0,0,0,0.3); "
      "transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1); }\n"
      ".color-indicator:hover { transform: scale(1.15); border-color: "
      "rgba(255,255,255,0.6); }\n"
      ".color-0 { background-color: #f59e0b; }\n"
      ".color-1 { background-color: #3b82f6; }\n"
      ".color-2 { background-color: #10b981; }\n"
      ".color-3 { background-color: #ef4444; }\n"
      ".color-4 { background-color: #8b5cf6; }\n"
      ".equation-entry { font-family: 'Inter', 'Segoe UI', system-ui, "
      "sans-serif; font-size: 15px; border: none; background: transparent; "
      "color: #eee; padding-left: 8px; }\n"
      ".equation-entry:focus { color: #fff; }\n"
      ".param-label { color: #888; font-family: monospace; font-weight: bold; "
      "min-width: 25px; }\n"
      ".slider-bound { color: #666; font-size: 11px; font-family: monospace; "
      "}\n"
      ".slider-value { color: #f59e0b; font-size: 12px; font-family: "
      "monospace; min-width: 40px; }\n"
      ".header-title { font-weight: bold; font-size: 18px; color: #ccc; }\n"
      ".delete-btn { background: transparent; border: none; opacity: 0.4; }\n"
      ".delete-btn:hover { opacity: 1.0; color: #f87171; }\n"
      ".result-label { color: #888; font-size: 13px; margin-top: -4px; "
      "margin-bottom: 4px; font-family: 'Inter', sans-serif; }\n"
      ".settings-btn { color: rgba(255,255,255,0.7); "
      "background: rgba(40,40,40,0.6); border-radius: 8px; border: 1px "
      "solid rgba(255,255,255,0.1); padding: 6px; transition: all 0.2s; }\n"
      ".settings-btn:hover { color: #fff; background: rgba(60,60,60,0.9); "
      "transform: scale(1.05); }\n"
      ".settings-popover { background-color: #1a1a1a; }\n"
      ".settings-section-title { font-weight: bold; font-size: 13px; "
      "color: #aaa; margin-top: 8px; margin-bottom: 4px; }\n"
      ".settings-check { color: #ccc; font-size: 12px; }\n"
      ".settings-label { color: #999; font-size: 12px; }\n"
      ".settings-spin { font-size: 11px; min-width: 60px; }\n"
      ".slider-play-btn:hover { opacity: 1.0; }\n"
      ".slider-bound-entry { font-size: 11px; font-family: monospace; "
      "background: rgba(40,40,40,0.4); border: 1px solid #333; color: #ccc; "
      "border-radius: 4px; padding: 0 4px; }\n"
      ".slider-param-label { color: #888; font-size: 10px; font-weight: bold; "
      "text-transform: uppercase; margin-right: 2px; }\n"
      ".slider-error-text { color: #f87171; font-size: 11px; font-style: "
      "italic; "
      "margin-top: 2px; }\n"
      ".mode-btn { padding: 4px 12px; font-size: 12px; border-radius: 4px; }\n"
      ".mode-btn-active { background-color: #3b82f6; color: white; }\n");
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "C++ Graphing Calculator");

  // screen size and window prop
  GdkDisplay *display = gdk_display_get_default();
  GListModel *monitors = gdk_display_get_monitors(display);
  GdkMonitor *monitor = GDK_MONITOR(g_list_model_get_item(monitors, 0));
  GdkRectangle geom;
  if (monitor) {
    gdk_monitor_get_geometry(monitor, &geom);
    int win_w = (int)(geom.width * 0.75);
    int win_h = (int)(geom.height * 0.75);
    gtk_window_set_default_size(GTK_WINDOW(window), win_w, win_h);
    g_object_unref(monitor);
  } else {
    gtk_window_set_default_size(GTK_WINDOW(window), 1200, 800);
  }

  GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_window_set_child(GTK_WINDOW(window), paned);
  gtk_paned_set_position(GTK_PANED(paned), 280);

  // left panel eq
  GtkWidget *left_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_widget_set_size_request(left_vbox, 250, -1);
  gtk_widget_add_css_class(left_vbox, "sidebar");
  gtk_paned_set_start_child(GTK_PANED(paned), left_vbox);

  GtkWidget *header_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_widget_set_margin_top(header_box, 15);
  gtk_widget_set_margin_bottom(header_box, 15);
  gtk_widget_set_margin_start(header_box, 15);
  gtk_widget_set_margin_end(header_box, 15);
  GtkWidget *eq_label = gtk_label_new("Expressions");
  gtk_widget_add_css_class(eq_label, "header-title");
  gtk_widget_set_hexpand(eq_label, TRUE);
  gtk_widget_set_halign(eq_label, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(header_box), eq_label);

  gtk_box_append(GTK_BOX(left_vbox), header_box);

  GtkWidget *scroll = gtk_scrolled_window_new();
  gtk_widget_set_vexpand(scroll, TRUE);
  gtk_box_append(GTK_BOX(left_vbox), scroll);

  equations_vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), equations_vbox);

  GtkWidget *add_btn = gtk_button_new_with_label("+ Add Expression");
  gtk_widget_set_margin_top(add_btn, 10);
  gtk_widget_set_margin_bottom(add_btn, 10);
  gtk_widget_set_margin_start(add_btn, 10);
  gtk_widget_set_margin_end(add_btn, 10);
  gtk_widget_add_css_class(add_btn, "suggested-action");
  gtk_box_append(GTK_BOX(left_vbox), add_btn);

  // right panel graph overlay settings
  GtkWidget *overlay = gtk_overlay_new();
  gtk_paned_set_end_child(GTK_PANED(paned), overlay);

  GtkWidget *area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(area, TRUE);
  gtk_widget_set_vexpand(area, TRUE);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(area), on_draw, NULL, NULL);
  gtk_overlay_set_child(GTK_OVERLAY(overlay), area);

  GtkWidget *settings_btn =
      gtk_button_new_from_icon_name("emblem-system-symbolic");
  gtk_widget_add_css_class(settings_btn, "settings-btn");
  gtk_widget_set_halign(settings_btn, GTK_ALIGN_START);
  gtk_widget_set_valign(settings_btn, GTK_ALIGN_START);
  gtk_widget_set_margin_top(settings_btn, 10);
  gtk_widget_set_margin_start(settings_btn, 10);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), settings_btn);

  // settings popover
  GtkWidget *popover = gtk_popover_new();
  gtk_widget_set_parent(popover, settings_btn);
  gtk_widget_add_css_class(popover, "settings-popover");

  GtkWidget *settings_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top(settings_box, 12);
  gtk_widget_set_margin_bottom(settings_box, 12);
  gtk_widget_set_margin_start(settings_box, 12);
  gtk_widget_set_margin_end(settings_box, 12);
  gtk_widget_set_size_request(settings_box, 260, -1);

  // grid section
  GtkWidget *grid_title = gtk_label_new("Grid");
  gtk_widget_add_css_class(grid_title, "settings-section-title");
  gtk_widget_set_halign(grid_title, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(settings_box), grid_title);

  GtkWidget *grid_check = gtk_check_button_new_with_label("Grid");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(grid_check),
                              plotter.settings.show_grid);
  gtk_widget_add_css_class(grid_check, "settings-check");
  g_signal_connect(grid_check, "toggled", G_CALLBACK(on_settings_grid_toggled),
                   area);
  gtk_box_append(GTK_BOX(settings_box), grid_check);

  GtkWidget *grid_opts = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
  GtkWidget *axis_nums_check = gtk_check_button_new_with_label("Axis Numbers");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(axis_nums_check),
                              plotter.settings.show_axis_numbers);
  gtk_widget_add_css_class(axis_nums_check, "settings-check");
  g_signal_connect(axis_nums_check, "toggled",
                   G_CALLBACK(on_settings_axis_nums_toggled), area);
  gtk_box_append(GTK_BOX(grid_opts), axis_nums_check);

  GtkWidget *minor_grid_check =
      gtk_check_button_new_with_label("Minor Gridlines");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(minor_grid_check),
                              plotter.settings.show_minor_gridlines);
  gtk_widget_add_css_class(minor_grid_check, "settings-check");
  g_signal_connect(minor_grid_check, "toggled",
                   G_CALLBACK(on_settings_minor_grid_toggled), area);
  gtk_box_append(GTK_BOX(grid_opts), minor_grid_check);
  gtk_box_append(GTK_BOX(settings_box), grid_opts);

  GtkWidget *arrows_check = gtk_check_button_new_with_label("Arrows");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(arrows_check),
                              plotter.settings.show_arrows);
  gtk_widget_add_css_class(arrows_check, "settings-check");
  g_signal_connect(arrows_check, "toggled",
                   G_CALLBACK(on_settings_arrows_toggled), area);
  gtk_box_append(GTK_BOX(settings_box), arrows_check);

  // x axis section
  GtkWidget *sep1 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(settings_box), sep1);

  GtkWidget *xaxis_title = gtk_label_new("X-Axis");
  gtk_widget_add_css_class(xaxis_title, "settings-section-title");
  gtk_widget_set_halign(xaxis_title, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(settings_box), xaxis_title);

  GtkWidget *x_range_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *x_min_lbl = gtk_label_new("Min:");
  gtk_widget_add_css_class(x_min_lbl, "settings-label");
  GtkWidget *x_min_spin = gtk_spin_button_new_with_range(-10000, 10000, 1);
  settings_ui.x_min_spin = x_min_spin;
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(x_min_spin), x_axis_min);
  gtk_widget_add_css_class(x_min_spin, "settings-spin");
  g_signal_connect(x_min_spin, "value-changed", G_CALLBACK(on_x_min_changed),
                   area);

  GtkWidget *x_max_lbl = gtk_label_new("Max:");
  gtk_widget_add_css_class(x_max_lbl, "settings-label");
  GtkWidget *x_max_spin = gtk_spin_button_new_with_range(-10000, 10000, 1);
  settings_ui.x_max_spin = x_max_spin;
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(x_max_spin), x_axis_max);
  gtk_widget_add_css_class(x_max_spin, "settings-spin");
  g_signal_connect(x_max_spin, "value-changed", G_CALLBACK(on_x_max_changed),
                   area);

  GtkWidget *x_step_lbl = gtk_label_new("Step:");
  gtk_widget_add_css_class(x_step_lbl, "settings-label");
  GtkWidget *x_step_spin = gtk_spin_button_new_with_range(0.01, 100, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(x_step_spin),
                            plotter.settings.x_step);
  gtk_widget_add_css_class(x_step_spin, "settings-spin");
  g_signal_connect(x_step_spin, "value-changed", G_CALLBACK(on_x_step_changed),
                   area);

  gtk_box_append(GTK_BOX(x_range_box), x_min_lbl);
  gtk_box_append(GTK_BOX(x_range_box), x_min_spin);
  gtk_box_append(GTK_BOX(x_range_box), x_max_lbl);
  gtk_box_append(GTK_BOX(x_range_box), x_max_spin);
  gtk_box_append(GTK_BOX(x_range_box), x_step_lbl);
  gtk_box_append(GTK_BOX(x_range_box), x_step_spin);
  gtk_box_append(GTK_BOX(settings_box), x_range_box);

  // y axis section
  GtkWidget *sep2 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(settings_box), sep2);

  GtkWidget *yaxis_title = gtk_label_new("Y-Axis");
  gtk_widget_add_css_class(yaxis_title, "settings-section-title");
  gtk_widget_set_halign(yaxis_title, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(settings_box), yaxis_title);

  GtkWidget *y_range_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
  GtkWidget *y_min_lbl = gtk_label_new("Min:");
  gtk_widget_add_css_class(y_min_lbl, "settings-label");
  GtkWidget *y_min_spin = gtk_spin_button_new_with_range(-10000, 10000, 1);
  settings_ui.y_min_spin = y_min_spin;
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(y_min_spin), y_axis_min);
  gtk_widget_add_css_class(y_min_spin, "settings-spin");
  g_signal_connect(y_min_spin, "value-changed", G_CALLBACK(on_y_min_changed),
                   area);

  GtkWidget *y_max_lbl = gtk_label_new("Max:");
  gtk_widget_add_css_class(y_max_lbl, "settings-label");
  GtkWidget *y_max_spin = gtk_spin_button_new_with_range(-10000, 10000, 1);
  settings_ui.y_max_spin = y_max_spin;
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(y_max_spin), y_axis_max);
  gtk_widget_add_css_class(y_max_spin, "settings-spin");
  g_signal_connect(y_max_spin, "value-changed", G_CALLBACK(on_y_max_changed),
                   area);

  GtkWidget *y_step_lbl = gtk_label_new("Step:");
  gtk_widget_add_css_class(y_step_lbl, "settings-label");
  GtkWidget *y_step_spin = gtk_spin_button_new_with_range(0.01, 100, 0.1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(y_step_spin),
                            plotter.settings.y_step);
  gtk_widget_add_css_class(y_step_spin, "settings-spin");
  g_signal_connect(y_step_spin, "value-changed", G_CALLBACK(on_y_step_changed),
                   area);

  gtk_box_append(GTK_BOX(y_range_box), y_min_lbl);
  gtk_box_append(GTK_BOX(y_range_box), y_min_spin);
  gtk_box_append(GTK_BOX(y_range_box), y_max_lbl);
  gtk_box_append(GTK_BOX(y_range_box), y_max_spin);
  gtk_box_append(GTK_BOX(y_range_box), y_step_lbl);
  gtk_box_append(GTK_BOX(y_range_box), y_step_spin);
  gtk_box_append(GTK_BOX(settings_box), y_range_box);

  // more options
  GtkWidget *sep3 = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
  gtk_box_append(GTK_BOX(settings_box), sep3);

  GtkWidget *more_title = gtk_label_new("More Options");
  gtk_widget_add_css_class(more_title, "settings-section-title");
  gtk_widget_set_halign(more_title, GTK_ALIGN_START);
  gtk_box_append(GTK_BOX(settings_box), more_title);

  GtkWidget *lock_check = gtk_check_button_new_with_label("Lock Viewport");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(lock_check),
                              plotter.settings.lock_viewport);
  gtk_widget_add_css_class(lock_check, "settings-check");
  g_signal_connect(lock_check, "toggled",
                   G_CALLBACK(on_settings_lock_viewport_toggled), area);
  gtk_box_append(GTK_BOX(settings_box), lock_check);

  GtkWidget *axes_check = gtk_check_button_new_with_label("Show Main Axes");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(axes_check),
                              plotter.settings.show_main_axis);
  gtk_widget_add_css_class(axes_check, "settings-check");
  g_signal_connect(axes_check, "toggled",
                   G_CALLBACK(on_settings_main_axis_toggled), area);
  gtk_box_append(GTK_BOX(settings_box), axes_check);

  GtkWidget *aspect_check =
      gtk_check_button_new_with_label("Lock Aspect Ratio");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(aspect_check),
                              plotter.settings.lock_aspect_ratio);
  gtk_widget_add_css_class(aspect_check, "settings-check");
  g_signal_connect(aspect_check, "toggled",
                   G_CALLBACK(on_settings_lock_aspect_toggled), area);
  gtk_box_append(GTK_BOX(settings_box), aspect_check);

  GtkWidget *angle_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  GtkWidget *rad_btn = gtk_check_button_new_with_label("Radians");
  GtkWidget *deg_btn = gtk_check_button_new_with_label("Degrees");
  gtk_check_button_set_group(GTK_CHECK_BUTTON(deg_btn),
                             GTK_CHECK_BUTTON(rad_btn));
  gtk_check_button_set_active(
      GTK_CHECK_BUTTON(plotter.settings.use_radians ? rad_btn : deg_btn), TRUE);
  gtk_widget_add_css_class(rad_btn, "settings-check");
  gtk_widget_add_css_class(deg_btn, "settings-check");
  g_signal_connect(rad_btn, "toggled", G_CALLBACK(on_settings_radians_toggled),
                   area);
  gtk_box_append(GTK_BOX(angle_box), rad_btn);
  gtk_box_append(GTK_BOX(angle_box), deg_btn);
  gtk_box_append(GTK_BOX(settings_box), angle_box);

  gtk_popover_set_child(GTK_POPOVER(popover), settings_box);

  // connect settings btn to popover
  g_signal_connect_swapped(settings_btn, "clicked",
                           G_CALLBACK(gtk_popover_popup), popover);

  g_object_set_data(G_OBJECT(add_btn), "vbox", equations_vbox);
  g_object_set_data(G_OBJECT(add_btn), "area", area);
  g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_clicked), NULL);

  // initial eq row
  add_equation_row(equations_vbox, area);

  // gestures
  GtkGesture *drag = gtk_gesture_drag_new();
  g_signal_connect(drag, "drag-begin", G_CALLBACK(on_drag_begin), area);
  g_signal_connect(drag, "drag-update", G_CALLBACK(on_drag_update), area);
  gtk_widget_add_controller(area, GTK_EVENT_CONTROLLER(drag));

  GtkEventController *scroll_ctrl =
      gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL);
  g_signal_connect(scroll_ctrl, "scroll", G_CALLBACK(on_scroll), area);
  gtk_widget_add_controller(area, scroll_ctrl);

  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
  GtkApplication *app =
      gtk_application_new("org.example.graphcalc", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);
  int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
