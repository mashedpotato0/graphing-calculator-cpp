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

// Viewport settings for UI
static double x_axis_min = -10, x_axis_max = 10;
static double y_axis_min = -10, y_axis_max = 10;

// Slider animation state
struct SliderAnimData {
  GtkRange *range;
  double min_val, max_val, step;
  guint timer_id;
  bool playing;
  GtkWidget *play_btn;
};
static std::vector<std::unique_ptr<SliderAnimData>> slider_anims;

static gboolean slider_anim_tick(gpointer data) {
  SliderAnimData *anim = static_cast<SliderAnimData *>(data);
  if (!anim->playing)
    return G_SOURCE_REMOVE;
  double val = gtk_range_get_value(anim->range);
  val += anim->step;
  if (val > anim->max_val)
    val = anim->min_val;
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
  GtkWidget *slider_box; // For parameter sliders
  GtkWidget *result_label;
  GtkWidget *symbolic_result_area;
  std::unique_ptr<expr> ast;
  std::unique_ptr<expr> result_ast; // For symbolic results
  std::string original_expression;  // For label and assignment logic
  std::string error;
  int color_idx;
  bool visible = true;
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

static void update_plotter(GtkWidget *area) {
  eval_engine.ctx.use_radians = plotter.settings.use_radians;

  const double COLORS[][3] = {
      {0.96, 0.62, 0.04}, // orange
      {0.38, 0.65, 0.98}, // blue
      {0.20, 0.83, 0.60}, // green
      {0.97, 0.44, 0.44}, // red
      {0.75, 0.52, 0.99}  // purple
  };

  // First pass: Process global assignments (e.g., a = 5, f(x) = sin(x))
  eval_engine.ctx.vars.clear();
  eval_engine.ctx.funcs.clear();

  for (const auto &eq : equations) {
    std::string s = eq->editor->get_expression();
    if (s.empty())
      continue;

    size_t eq_pos = s.find('=');
    if (eq_pos != std::string::npos) {
      std::string lhs = s.substr(0, eq_pos);
      std::string rhs = s.substr(eq_pos + 1);
      // Trim lhs
      lhs.erase(0, lhs.find_first_not_of(" \t"));
      lhs.erase(lhs.find_last_not_of(" \t") + 1);

      // Simple assignment check: LHS is a single word
      bool is_assignment = true;
      if (lhs.empty())
        is_assignment = false;
      size_t last_non_ws = lhs.find_last_not_of(" \t");
      if (last_non_ws != std::string::npos)
        lhs.erase(last_non_ws + 1);
      else
        lhs.clear();
      for (char c : lhs)
        if (!std::isalnum(c) && c != '_' && c != '(' && c != ')' && c != ',') {
          is_assignment = false;
          break;
        }

      if (is_assignment && lhs != "x" && lhs != "y") {
        size_t paren_open = lhs.find('(');
        if (paren_open != std::string::npos) {
          // Function assignment: f(x) = ... or g(x,y) = ...
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
          // Variable assignment: a = 5
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

    // Smart strip: y = ..., f(x) = ..., etc.
    size_t eq_pos = s.find('=');
    std::string clean_s = s;
    bool is_plot = true;
    bool is_implicit = false;

    if (eq_pos != std::string::npos) {
      std::string lhs = s.substr(0, eq_pos);
      std::string rhs = s.substr(eq_pos + 1);

      // Trim lhs
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
      } else if (lhs != "x" && lhs.find('(') == std::string::npos &&
                 lhs.find_first_of("+-*/") == std::string::npos) {
        // Global assignment
        is_plot = false;
      } else {
        // Equation like x^2+y^2=1 or a+bx=0
        clean_s = "(" + lhs + ")-(" + rhs + ")";
        is_implicit = true;
      }
    } else {
      clean_s = s;
      is_implicit = false;
    }

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
                                           eq->visible, is_implicit, s});

            // Numerical result display
            std::set<std::string> vars;
            plotter.expressions.back().ast->collect_variables(vars);
            // If no x or y, it's a constant or definite op
            if (vars.find("x") == vars.end() && vars.find("y") == vars.end()) {
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

            // Symbolic result display (for derivatives, integrals, or
            // user-defined functions)
            if (ast) {
              try {
                // Determine if we should show a symbolic result
                bool show_symbolic = false;
                if (dynamic_cast<const deriv_node *>(ast.get()) ||
                    dynamic_cast<const integral *>(ast.get()) ||
                    dynamic_cast<const func_call *>(ast.get())) {
                  show_symbolic = true;
                }

                if (show_symbolic) {
                  auto sym_res = ast->expand(eval_engine.ctx)->simplify();
                  // Only show if it's different from the input OR if it's
                  // simplified a lot
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

      // Update sliders for this equation
      GtkWidget *area = static_cast<GtkWidget *>(
          g_object_get_data(G_OBJECT(eq_data->editor_area), "drawing-area"));
      sync_sliders(eq_data, area);
    }
  } catch (const std::exception &e) {
    eq_data->ast = nullptr;
    eq_data->error = e.what();
  }

  // Refresh plotter
  GtkWidget *area = static_cast<GtkWidget *>(
      g_object_get_data(G_OBJECT(eq_data->editor_area), "drawing-area"));
  update_plotter(area);
}

static void on_editor_draw(GtkDrawingArea *area, cairo_t *cr, int width,
                           int height, gpointer data) {
  (void)area;
  (void)width;
  EquationData *eq = static_cast<EquationData *>(data);
  // draw background transparent for standard style, but we can clear with
  // transparent
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
    cairo_set_source_rgba(cr, 0.53, 0.53, 0.53, 1.0); // opacity 0.53ish gray
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
    // Jump into lower bound of preceding integral (if any)
    if (editor->cursor_index > 0) {
      auto *prev = dynamic_cast<MathIntegral *>(
          editor->active_box->nodes[editor->cursor_index - 1].get());
      if (prev) {
        editor->active_box = prev->lower.get();
        editor->cursor_index = 0;
      }
    }
  } else if (keyval == GDK_KEY_Return) {
    // Create new line maybe?
  } else {
    // printable ASCII characters
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

// End of on_editor_click navigation logic

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
  if (val_label) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", value);
    gtk_label_set_text(GTK_LABEL(val_label), buf);
  }

  EquationData *eq = static_cast<EquationData *>(
      g_object_get_data(G_OBJECT(range), "eq_data"));
  if (eq) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s = %.2f", var_name, value);
    // Remove backslashes before passing letter-by-letter to MathEditor,
    // so its macro detection correctly generates MathSymbols!
    eq->editor->clear();
    for (char c : std::string(buf)) {
      if (c != '\\') {
        eq->editor->insert_char(std::string(1, c));
      }
    }
    gtk_widget_queue_draw(eq->editor_area);

    // Also update the AST so the plot changes immediately
    auto tokens = tokenize(buf);
    parser p(tokens);
    try {
      eq->ast = p.parse_expr();
    } catch (...) {
    }
  }

  GtkWidget *area =
      GTK_WIDGET(g_object_get_data(G_OBJECT(range), "drawing-area"));
  gtk_widget_queue_draw(area);
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

static void on_slider_min_changed(GtkSpinButton *spin, gpointer data) {
  GtkRange *range = GTK_RANGE(data);
  double min_val = gtk_spin_button_get_value(spin);
  GtkAdjustment *adj = gtk_range_get_adjustment(range);
  gtk_adjustment_set_lower(adj, min_val);

  SliderAnimData *anim =
      static_cast<SliderAnimData *>(g_object_get_data(G_OBJECT(range), "anim"));
  if (anim)
    anim->min_val = min_val;
}

static void on_slider_max_changed(GtkSpinButton *spin, gpointer data) {
  GtkRange *range = GTK_RANGE(data);
  double max_val = gtk_spin_button_get_value(spin);
  GtkAdjustment *adj = gtk_range_get_adjustment(range);
  gtk_adjustment_set_upper(adj, max_val);

  SliderAnimData *anim =
      static_cast<SliderAnimData *>(g_object_get_data(G_OBJECT(range), "anim"));
  if (anim)
    anim->max_val = max_val;
}

static void sync_sliders(EquationData *eq, GtkWidget *drawing_area) {
  // Clear existing sliders in this eq box
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

  // Check if it's a simple assignment (single word LHS, no parenthesis)
  bool is_assignment = !lhs.empty();
  for (char c : lhs)
    if (!std::isalnum(c) && c != '_' && c != '\\') {
      is_assignment = false;
      break;
    }
  if (lhs == "y" || lhs.find('(') != std::string::npos)
    is_assignment = false;

  if (!is_assignment)
    return;

  const std::string &v = lhs;

  if (eval_engine.ctx.vars.find(v) == eval_engine.ctx.vars.end()) {
    eval_engine.ctx.vars[v] = 1.0;
  }

  double cur_val = eval_engine.ctx.vars[v];
  double default_min = -10.0, default_max = 10.0;

  // Slider row with play button
  GtkWidget *slider_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
  gtk_widget_set_margin_start(slider_hbox, 4);
  gtk_widget_set_margin_end(slider_hbox, 4);

  // Play button
  auto anim = std::make_unique<SliderAnimData>();
  GtkWidget *play_btn =
      gtk_button_new_from_icon_name("media-playback-start-symbolic");
  gtk_widget_add_css_class(play_btn, "flat");
  gtk_widget_add_css_class(play_btn, "slider-play-btn");
  gtk_widget_set_size_request(play_btn, 24, 24);

  // Min bound spin button
  GtkWidget *min_spin = gtk_spin_button_new_with_range(-1000, 1000, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(min_spin), default_min);
  gtk_widget_set_size_request(min_spin, 55, -1);
  gtk_widget_add_css_class(min_spin, "slider-bound-spin");

  // Slider
  GtkWidget *slider = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL,
                                               default_min, default_max, 0.1);
  gtk_range_set_value(GTK_RANGE(slider), cur_val);
  gtk_widget_set_hexpand(slider, TRUE);
  gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);

  // Max bound spin button
  GtkWidget *max_spin = gtk_spin_button_new_with_range(-1000, 1000, 1);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(max_spin), default_max);
  gtk_widget_set_size_request(max_spin, 55, -1);
  gtk_widget_add_css_class(max_spin, "slider-bound-spin");

  // Value label
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

  // Connect min/max spin buttons to adjust slider range
  g_signal_connect(min_spin, "value-changed", G_CALLBACK(on_slider_min_changed),
                   slider);
  g_signal_connect(max_spin, "value-changed", G_CALLBACK(on_slider_max_changed),
                   slider);

  // Setup animation data
  anim->range = GTK_RANGE(slider);
  anim->min_val = default_min;
  anim->max_val = default_max;
  anim->step = 0.1;
  anim->timer_id = 0;
  anim->playing = false;
  anim->play_btn = play_btn;

  SliderAnimData *anim_ptr = anim.get();
  slider_anims.push_back(std::move(anim));

  g_signal_connect(play_btn, "clicked", G_CALLBACK(on_play_clicked), anim_ptr);

  gtk_box_append(GTK_BOX(slider_hbox), play_btn);
  gtk_box_append(GTK_BOX(slider_hbox), min_spin);
  gtk_box_append(GTK_BOX(slider_hbox), slider);
  gtk_box_append(GTK_BOX(slider_hbox), max_spin);
  gtk_box_append(GTK_BOX(eq->slider_box), slider_hbox);
}

static void on_color_pressed(GtkGestureClick *gesture, int n_press, double x,
                             double y, gpointer data) {
  (void)n_press;
  (void)x;
  (void)y;
  EquationData *eq = (EquationData *)data;
  guint button =
      gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));

  if (button == 1) { // Left click
    eq->visible = !eq->visible;
    gtk_widget_set_opacity(eq->color_btn, eq->visible ? 1.0 : 0.3);
    update_plotter(eq->editor_area);
  } else if (button == 3) { // Right click
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
              [](GtkButton *b, gpointer d) {
                ColorChoice *c = (ColorChoice *)d;
                char old_c[32], new_c[32];
                snprintf(old_c, sizeof(old_c), "color-%d",
                         c->eq->color_idx % 5);
                snprintf(new_c, sizeof(new_c), "color-%d", c->idx % 5);
                gtk_widget_remove_css_class(c->eq->color_btn, old_c);
                gtk_widget_add_css_class(c->eq->color_btn, new_c);
                c->eq->color_idx = c->idx;
                update_plotter(c->eq->editor_area);
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

  eq->row_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
  gtk_widget_add_css_class(eq->row_box, "equation-row");
  gtk_widget_set_margin_bottom(eq->row_box, 10);

  GtkWidget *top_hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);

  // Color indicator
  eq->color_btn = gtk_button_new();
  gtk_widget_set_size_request(eq->color_btn, 26, 26);
  gtk_widget_set_valign(eq->color_btn, GTK_ALIGN_CENTER);
  gtk_widget_set_halign(eq->color_btn, GTK_ALIGN_CENTER);
  gtk_widget_set_margin_start(eq->color_btn, 4);
  char color_class[32];
  snprintf(color_class, sizeof(color_class), "color-%d", eq->color_idx % 5);
  gtk_widget_add_css_class(eq->color_btn, "color-indicator");
  gtk_widget_add_css_class(eq->color_btn, color_class);

  GtkGesture *color_click = gtk_gesture_click_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(color_click),
                                0); // All buttons
  g_signal_connect(color_click, "pressed", G_CALLBACK(on_color_pressed), eq);
  gtk_widget_add_controller(eq->color_btn, GTK_EVENT_CONTROLLER(color_click));

  eq->editor = std::make_unique<MathEditor>();
  eq->editor_area = gtk_drawing_area_new();
  gtk_widget_set_hexpand(eq->editor_area, TRUE);
  gtk_widget_set_size_request(eq->editor_area, -1, 50); // 50px height initially
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

  g_object_set_data(G_OBJECT(eq->editor_area), "drawing-area", drawing_area);
  g_object_set_data(G_OBJECT(eq->editor_area), "eq_data", eq);

  g_object_set_data(G_OBJECT(eq->editor_area), "vbox", vbox);
  g_object_set_data(G_OBJECT(eq->editor_area), "area", drawing_area);
  // g_signal_connect(eq->editor_area, "activate", G_CALLBACK(on_add_clicked),
  // NULL);

  g_object_set_data(G_OBJECT(eq->delete_btn), "drawing-area", drawing_area);
  g_signal_connect(eq->delete_btn, "clicked", G_CALLBACK(on_delete_clicked),
                   eq);

  gtk_box_append(GTK_BOX(vbox), eq->row_box);
  gtk_widget_grab_focus(eq->editor_area);

  equations.push_back(std::move(eq_ptr));
}

// Interactivity logic below...

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
  // x,y are offsets from drag start point
  plotter.center_x = drag_start_center_x - x / plotter.zoom_x;
  plotter.center_y = drag_start_center_y + y / plotter.zoom_y;
  gtk_widget_queue_draw(area);
}

static gboolean on_scroll(GtkEventControllerScroll *scroll, double dx,
                          double dy, gpointer data) {
  (void)scroll;
  (void)dx;
  if (plotter.settings.lock_viewport)
    return TRUE;
  GtkWidget *area = GTK_WIDGET(data);

  // Get mouse position for zoom-toward-cursor
  int w = gtk_widget_get_width(area);
  int h = gtk_widget_get_height(area);

  // Get the current cursor position via the scroll controller
  double mx = w / 2.0, my = h / 2.0; // fallback to center
  GdkEvent *event =
      gtk_event_controller_get_current_event(GTK_EVENT_CONTROLLER(scroll));
  if (event) {
    double ex, ey;
    if (gdk_event_get_position(event, &ex, &ey)) {
      // Coordinates are relative to the widget's surface
      mx = ex;
      my = ey;
    }
  }

  // Convert mouse position to math coordinates before zoom
  double math_x = (mx - w / 2.0) / plotter.zoom_x + plotter.center_x;
  double math_y = (h / 2.0 - my) / plotter.zoom_y + plotter.center_y;

  // Apply zoom
  double factor = (dy < 0) ? 1.1 : 1.0 / 1.1;
  plotter.zoom_x *= factor;
  plotter.zoom_y *= factor;

  // Adjust center so the math point stays under the cursor
  plotter.center_x = math_x - (mx - w / 2.0) / plotter.zoom_x;
  plotter.center_y = math_y + (my - h / 2.0) / plotter.zoom_y;

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
static void on_settings_axes_toggled(GtkCheckButton *btn, gpointer data) {
  plotter.settings.show_main_axes = gtk_check_button_get_active(btn);
  GtkWidget *area = GTK_WIDGET(data);
  gtk_widget_queue_draw(area);
}
static void on_settings_lock_viewport_toggled(GtkCheckButton *btn, gpointer) {
  plotter.settings.lock_viewport = gtk_check_button_get_active(btn);
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
  x_axis_min = gtk_spin_button_get_value(spin);
  GtkWidget *area = GTK_WIDGET(data);
  int w = gtk_widget_get_width(area);
  double range_x = x_axis_max - x_axis_min;
  if (range_x > 0) {
    plotter.center_x = (x_axis_min + x_axis_max) / 2.0;
    plotter.zoom_x = w / range_x;
    if (plotter.settings.lock_aspect_ratio) {
      plotter.zoom_y = plotter.zoom_x;
      // Adjust y bounds to show locked aspect
      int h = gtk_widget_get_height(area);
      double range_y = h / plotter.zoom_y;
      y_axis_min = plotter.center_y - range_y / 2.0;
      y_axis_max = plotter.center_y + range_y / 2.0;
    }
  }
  gtk_widget_queue_draw(area);
}
static void on_x_max_changed(GtkSpinButton *spin, gpointer data) {
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
    }
  }
  gtk_widget_queue_draw(area);
}
static void on_y_min_changed(GtkSpinButton *spin, gpointer data) {
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
    }
  }
  gtk_widget_queue_draw(area);
}
static void on_y_max_changed(GtkSpinButton *spin, gpointer data) {
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
    }
  }
  gtk_widget_queue_draw(area);
}
static void on_x_step_changed(GtkSpinButton *spin, gpointer) {
  plotter.settings.x_step = gtk_spin_button_get_value(spin);
}
static void on_y_step_changed(GtkSpinButton *spin, gpointer) {
  plotter.settings.y_step = gtk_spin_button_get_value(spin);
}

static void activate(GtkApplication *app, gpointer user_data) {
  (void)user_data;

  // Load CSS
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
      ".slider-play-btn { padding: 0; min-width: 24px; min-height: 24px; "
      "opacity: 0.6; }\n"
      ".slider-play-btn:hover { opacity: 1.0; }\n"
      ".slider-bound-spin { font-size: 11px; font-family: monospace; }\n"
      ".mode-btn { padding: 4px 12px; font-size: 12px; border-radius: 4px; }\n"
      ".mode-btn-active { background-color: #3b82f6; color: white; }\n");
  gtk_style_context_add_provider_for_display(
      gdk_display_get_default(), GTK_STYLE_PROVIDER(provider),
      GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "C++ Graphing Calculator");

  // Get screen size and set window proportionally
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

  // Left Panel: Equations
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

  // Right Panel: Graph with Overlay Settings
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
  gtk_widget_set_halign(settings_btn, GTK_ALIGN_END);
  gtk_widget_set_valign(settings_btn, GTK_ALIGN_START);
  gtk_widget_set_margin_top(settings_btn, 10);
  gtk_widget_set_margin_end(settings_btn, 10);
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), settings_btn);

  // Build settings popover
  GtkWidget *popover = gtk_popover_new();
  gtk_widget_set_parent(popover, settings_btn);
  gtk_widget_add_css_class(popover, "settings-popover");

  GtkWidget *settings_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
  gtk_widget_set_margin_top(settings_box, 12);
  gtk_widget_set_margin_bottom(settings_box, 12);
  gtk_widget_set_margin_start(settings_box, 12);
  gtk_widget_set_margin_end(settings_box, 12);
  gtk_widget_set_size_request(settings_box, 260, -1);

  // --- Grid Section ---
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

  GtkWidget *axes_check = gtk_check_button_new_with_label("Main Axes");
  gtk_check_button_set_active(GTK_CHECK_BUTTON(axes_check),
                              plotter.settings.show_main_axes);
  gtk_widget_add_css_class(axes_check, "settings-check");
  g_signal_connect(axes_check, "toggled", G_CALLBACK(on_settings_axes_toggled),
                   area);
  gtk_box_append(GTK_BOX(settings_box), axes_check);

  // --- X-Axis Section ---
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
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(x_min_spin), x_axis_min);
  gtk_widget_add_css_class(x_min_spin, "settings-spin");
  g_signal_connect(x_min_spin, "value-changed", G_CALLBACK(on_x_min_changed),
                   area);

  GtkWidget *x_max_lbl = gtk_label_new("Max:");
  gtk_widget_add_css_class(x_max_lbl, "settings-label");
  GtkWidget *x_max_spin = gtk_spin_button_new_with_range(-10000, 10000, 1);
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

  // --- Y-Axis Section ---
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
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(y_min_spin), y_axis_min);
  gtk_widget_add_css_class(y_min_spin, "settings-spin");
  g_signal_connect(y_min_spin, "value-changed", G_CALLBACK(on_y_min_changed),
                   area);

  GtkWidget *y_max_lbl = gtk_label_new("Max:");
  gtk_widget_add_css_class(y_max_lbl, "settings-label");
  GtkWidget *y_max_spin = gtk_spin_button_new_with_range(-10000, 10000, 1);
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

  // --- More Options ---
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

  // Connect settings button to popover
  g_signal_connect_swapped(settings_btn, "clicked",
                           G_CALLBACK(gtk_popover_popup), popover);

  g_object_set_data(G_OBJECT(add_btn), "vbox", equations_vbox);
  g_object_set_data(G_OBJECT(add_btn), "area", area);
  g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_clicked), NULL);

  // Initial equation
  add_equation_row(equations_vbox, area);

  // Gestures
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
