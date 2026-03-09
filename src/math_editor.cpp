#include "math_editor.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

static RenderMetrics measure_text(cairo_t *cr, const std::string &txt,
                                  double size) {
  cairo_set_font_size(cr, size);
  cairo_text_extents_t ext;
  cairo_text_extents(cr, txt.c_str(), &ext);
  return {ext.x_advance, -ext.y_bearing, ext.height + ext.y_bearing, 1.0};
}

static void draw_text(cairo_t *cr, const std::string &txt, double x, double y,
                      double size) {
  cairo_set_font_size(cr, size);
  cairo_move_to(cr, x, y);
  cairo_show_text(cr, txt.c_str());
}

RenderMetrics MathGlyph::measure(cairo_t *cr, double font_size) {
  last_metrics = measure_text(cr, character, font_size);
  return last_metrics;
}

void MathGlyph::draw(cairo_t *cr, double x, double y, double font_size) {
  last_x = x;
  last_y = y;
  draw_text(cr, character, x, y, font_size);
}

RenderMetrics MathSymbol::measure(cairo_t *cr, double font_size) {
  last_metrics = measure_text(cr, display_text, font_size);
  return last_metrics;
}

void MathSymbol::draw(cairo_t *cr, double x, double y, double font_size) {
  last_x = x;
  last_y = y;
  draw_text(cr, display_text, x, y, font_size);
}

MathFraction::MathFraction() {
  numerator = std::make_unique<MathBox>();
  numerator->parent_node = this;
  denominator = std::make_unique<MathBox>();
  denominator->parent_node = this;
}

RenderMetrics MathFraction::measure(cairo_t *cr, double font_size) {
  auto num = numerator->measure(cr, font_size * 0.9);
  auto den = denominator->measure(cr, font_size * 0.9);

  last_metrics.width = std::max(num.width, den.width) + 10;
  last_metrics.ascent = num.height() + 4;
  last_metrics.descent = den.height() + 4;
  last_metrics.scale = 1.0;
  return last_metrics;
}

void MathFraction::draw(cairo_t *cr, double x, double y, double font_size) {
  last_x = x;
  last_y = y;

  double num_font = font_size * 0.9;
  double den_font = font_size * 0.9;

  double mid_x = x + last_metrics.width / 2.0;

  numerator->draw(cr, mid_x - numerator->last_metrics.width / 2.0,
                  y - 6 - numerator->last_metrics.descent, num_font);
  denominator->draw(cr, mid_x - denominator->last_metrics.width / 2.0,
                    y + 6 + denominator->last_metrics.ascent, den_font);

  cairo_set_line_width(cr, 1.5);
  cairo_move_to(cr, x + 2, y - 4);
  cairo_line_to(cr, x + last_metrics.width - 2, y - 4);
  cairo_stroke(cr);
}

std::string MathFraction::to_string() const {
  return "\\frac{" + numerator->to_string() + "}{" + denominator->to_string() +
         "}";
}

MathPower::MathPower() {
  exponent = std::make_unique<MathBox>();
  exponent->parent_node = this;
}

RenderMetrics MathPower::measure(cairo_t *cr, double font_size) {
  auto exp = exponent->measure(cr, font_size * 0.6);

  last_metrics.width = exp.width + 1;
  last_metrics.ascent = exp.height() + font_size * 0.5;
  last_metrics.descent = 0; // min descent
  last_metrics.scale = 1.0;
  return last_metrics;
}

void MathPower::draw(cairo_t *cr, double x, double y, double font_size) {
  last_x = x;
  last_y = y;

  double exp_font = font_size * 0.6;
  exponent->draw(cr, x, y - font_size * 0.7, exp_font);
}

std::string MathPower::to_string() const {
  return "^{" + exponent->to_string() + "}";
}

// sqrt
MathSqrt::MathSqrt() {
  content = std::make_unique<MathBox>();
  content->parent_node = this;
}
MathSqrt::~MathSqrt() = default;

RenderMetrics MathSqrt::measure(cairo_t *cr, double font_size) {
  auto m = content->measure(cr, font_size);
  last_metrics.width = m.width + font_size * 0.7; // radical room
  last_metrics.ascent = m.ascent + 4;
  last_metrics.descent = m.descent;
  last_metrics.scale = 1.0;
  return last_metrics;
}

void MathSqrt::draw(cairo_t *cr, double x, double y, double font_size) {
  last_x = x;
  last_y = y;

  // radical
  cairo_set_line_width(cr, 1.2);
  double h = last_metrics.ascent + last_metrics.descent;
  double top = y - last_metrics.ascent;

  cairo_move_to(cr, x + font_size * 0.1, y - h * 0.2);
  cairo_line_to(cr, x + font_size * 0.2, y + last_metrics.descent);
  cairo_line_to(cr, x + font_size * 0.4, top);
  cairo_line_to(cr, x + last_metrics.width, top);
  cairo_stroke(cr);

  content->draw(cr, x + font_size * 0.5, y, font_size);
}

std::string MathSqrt::to_string() const {
  return "sqrt(" + content->to_string() + ")";
}

// int
MathIntegral::MathIntegral() {
  lower = std::make_unique<MathBox>();
  lower->parent_node = this;
  upper = std::make_unique<MathBox>();
  upper->parent_node = this;
}

RenderMetrics MathIntegral::measure(cairo_t *cr, double font_size) {
  double sf = font_size * 0.55;
  auto lo = lower->measure(cr, sf);
  auto up = upper->measure(cr, sf);

  // int sym width
  double sym_w = font_size * 0.55;
  double bound_w = std::max(lo.width, up.width);
  last_metrics.width = sym_w + bound_w + 2;
  last_metrics.ascent = std::max(font_size * 0.9, up.height() + 2);
  last_metrics.descent = std::max(font_size * 0.3, lo.height() + 2);
  last_metrics.scale = 1.0;
  return last_metrics;
}

void MathIntegral::draw(cairo_t *cr, double x, double y, double font_size) {
  last_x = x;
  last_y = y;

  // int sym
  cairo_set_font_size(cr, font_size * 1.5);
  cairo_move_to(cr, x, y + font_size * 0.3);
  cairo_show_text(cr, "\u222b");

  double sym_w = font_size * 0.55;
  double sf = font_size * 0.55;

  // up bound top right of int
  upper->draw(cr, x + sym_w, y - font_size * 0.7, sf);
  // lo bound bot right of int
  lower->draw(cr, x + sym_w, y + font_size * 0.3, sf);
}

std::string MathIntegral::to_string() const {
  std::string s = "\\int ";
  std::string lo = lower->to_string();
  std::string up = upper->to_string();
  bool has_lo = lo != " " && !lo.empty();
  bool has_up = up != " " && !up.empty();
  if (has_lo)
    s += "_{" + lo + "}";
  if (has_up)
    s += "^{" + up + "}";
  s += " ";
  return s;
}

// box
RenderMetrics MathBox::measure(cairo_t *cr, double font_size) {
  if (nodes.empty()) {
    last_metrics = measure_text(cr, " ", font_size);
    last_metrics.width = 0; // placeholder
    return last_metrics;
  }

  last_metrics.width = 0;
  last_metrics.ascent = 0;
  last_metrics.descent = 0;

  for (auto &n : nodes) {
    auto m = n->measure(cr, font_size);
    last_metrics.width += m.width;
    last_metrics.ascent = std::max(last_metrics.ascent, m.ascent);
    last_metrics.descent = std::max(last_metrics.descent, m.descent);
  }
  return last_metrics;
}

void MathBox::draw(cairo_t *cr, double x, double y, double font_size) {
  last_x = x;
  last_y = y;

  double cur_x = x;
  for (auto &n : nodes) {
    n->draw(cr, cur_x, y, font_size);
    cur_x += n->last_metrics.width;
  }
}

std::string MathBox::to_string() const {
  std::string s;
  for (auto &n : nodes) {
    s += n->to_string();
  }
  return s.empty() ? " " : s;
}

void MathBox::insert(int index, std::unique_ptr<MathNode> node) {
  node->parent_box = this;
  nodes.insert(nodes.begin() + index, std::move(node));
}

void MathBox::remove(int index) {
  if (index >= 0 && index < (int)nodes.size()) {
    nodes.erase(nodes.begin() + index);
  }
}

// editor
MathEditor::MathEditor() {
  active_box = &root;
  cursor_index = 0;
}

void MathEditor::insert_char(const std::string &c) {
  active_box->insert(cursor_index, std::make_unique<MathGlyph>(c));
  cursor_index++;

  struct Keyword {
    std::string text;
    std::string display;
    std::string latex;
    bool is_integral = false;
    bool is_sqrt = false;
  };
  static const std::vector<Keyword> keywords = {
      {"int", "\u222b", "\\int ", true}, // struct int
      {"sum", "\u2211", "\\sum "},
      {"prod", "\u220f", "\\prod "},
      {"pi", "\u03c0", "\\pi "},
      {"alpha", "\u03b1", "\\alpha "},
      {"beta", "\u03b2", "\\beta "},
      {"gamma", "\u03b3", "\\gamma "},
      {"delta", "\u03b4", "\\delta "},
      {"theta", "\u03b8", "\\theta "},
      {"phi", "\u03c6", "\\phi "},
      {"tau", "\u03c4", "\\tau "},
      {"inf", "\u221e", "\\infty "},
      {"infty", "\u221e", "\\infty "},
      {"sqrt", "\u221a", "\\sqrt ", false, true}};

  for (const auto &kw : keywords) {
    int len = kw.text.length();
    if (cursor_index >= len) {
      bool match = true;
      for (int i = 0; i < len; ++i) {
        auto *glyph = dynamic_cast<MathGlyph *>(
            active_box->nodes[cursor_index - len + i].get());
        if (!glyph || glyph->character != std::string(1, kw.text[i])) {
          match = false;
          break;
        }
      }
      if (match) {
        for (int i = 0; i < len; ++i) {
          active_box->remove(cursor_index - len);
        }
        cursor_index -= len;
        if (kw.is_integral) {
          // insert struct int node
          active_box->insert(cursor_index, std::move(integ));
          cursor_index++;
        } else if (kw.is_sqrt) {
          insert_sqrt();
        } else {
          active_box->insert(
              cursor_index, std::make_unique<MathSymbol>(kw.display, kw.latex));
          cursor_index++;
        }
        break;
      }
    }
  }
}

void MathEditor::set_expression(const std::string &s) {
  clear();
  for (char c : s) {
    insert_char(std::string(1, c));
  }
}

void MathEditor::insert_fraction() {
  auto frac = std::make_unique<MathFraction>();
  auto frac_ptr = frac.get();

  if (cursor_index > 0) {
    int start_idx = cursor_index - 1;
    // if ) cap balanced range
    if (active_box->nodes[cursor_index - 1]->to_string() == ")") {
      int depth = 0;
      for (int i = cursor_index - 1; i >= 0; --i) {
        std::string s = active_box->nodes[i]->to_string();
        if (s == ")")
          depth++;
        else if (s == "(") {
          depth--;
          if (depth == 0) {
            start_idx = i;
            break;
          }
        }
      }
    } else {
      // find alpha block
      for (int i = cursor_index - 1; i >= 0; --i) {
        std::string s = active_box->nodes[i]->to_string();
        if (s.empty() || (!std::isalnum(s[0]) && s[0] != '_')) {
          start_idx = i + 1;
          break;
        }
        start_idx = i;
      }
    }

    int count = cursor_index - start_idx;
    if (count > 0) {
      for (int i = 0; i < count; ++i) {
        auto node = std::move(active_box->nodes[start_idx]);
        node->parent_box = frac->numerator.get();
        frac->numerator->nodes.push_back(std::move(node));
        active_box->nodes.erase(active_box->nodes.begin() + start_idx);
      }
      cursor_index -= count;
    }
  }

  active_box->insert(cursor_index, std::move(frac));
  active_box = frac_ptr->denominator.get();
  cursor_index = 0;
}

void MathEditor::insert_power() {
  auto pow = std::make_unique<MathPower>();
  auto pow_ptr = pow.get();
  active_box->insert(cursor_index, std::move(pow));
  active_box = pow_ptr->exponent.get();
  cursor_index = 0;
}

void MathEditor::insert_integral() {
  auto integ = std::make_unique<MathIntegral>();
  auto integ_ptr = integ.get();
  active_box->insert(cursor_index, std::move(integ));
  cursor_index++;
  // cursor in main box use underscore to jump to lo bound
  (void)integ_ptr; // suppress warn
}

void MathEditor::backspace() {
  if (cursor_index > 0) {
    active_box->remove(cursor_index - 1);
    cursor_index--;
  } else {
    if (active_box->parent_node) {
      auto parent_box = active_box->parent_node->parent_box;

      auto it = std::find_if(parent_box->nodes.begin(), parent_box->nodes.end(),
                             [&](const std::unique_ptr<MathNode> &n) {
                               return n.get() == active_box->parent_node;
                             });

      if (it != parent_box->nodes.end()) {
        int dist = std::distance(parent_box->nodes.begin(), it);

        if (active_box->parent_node->is_fraction() &&
            active_box == static_cast<MathFraction *>(active_box->parent_node)
                              ->denominator.get()) {
          active_box = static_cast<MathFraction *>(active_box->parent_node)
                           ->numerator.get();
          cursor_index = active_box->nodes.size();
        } else if (active_box->nodes.empty()) {
          parent_box->remove(dist);
          active_box = parent_box;
          cursor_index = dist;
        } else {
          active_box = parent_box;
          cursor_index = dist;
        }
      }
    }
  }
}

// nav logic below
auto sqrt_node = std::make_unique<MathSqrt>();
auto sqrt_ptr = sqrt_node.get();

if (cursor_index > 0) {
  int start_idx = cursor_index - 1;
  if (active_box->nodes[cursor_index - 1]->to_string() == ")") {
    int depth = 0;
    for (int i = cursor_index - 1; i >= 0; --i) {
      std::string s = active_box->nodes[i]->to_string();
      if (s == ")")
        depth++;
      else if (s == "(") {
        depth--;
        if (depth == 0) {
          start_idx = i;
          break;
        }
      }
    }
  } else {
    for (int i = cursor_index - 1; i >= 0; --i) {
      std::string s = active_box->nodes[i]->to_string();
      if (!s.empty() && (std::isalnum(s[0]) || s[0] == '_' || s[0] == '.')) {
        start_idx = i;
      } else {
        break;
      }
    }
  }

  int count = cursor_index - start_idx;
  if (count > 0) {
    for (int i = 0; i < count; ++i) {
      sqrt_ptr->content->nodes.push_back(
          std::move(active_box->nodes[start_idx]));
      active_box->nodes.erase(active_box->nodes.begin() + start_idx);
    }
    for (auto &n : sqrt_ptr->content->nodes) {
      n->parent_box = sqrt_ptr->content.get();
    }
    cursor_index = start_idx;
  }
}

active_box->insert(cursor_index, std::move(sqrt_node));
active_box = sqrt_ptr->content.get();
cursor_index = active_box->nodes.size();
}

void MathEditor::delete_forward() {
  if (cursor_index < (int)active_box->nodes.size()) {
    active_box->remove(cursor_index);
  } else {
    if (active_box->parent_node) {
      auto parent_box = active_box->parent_node->parent_box;
      auto it = std::find_if(parent_box->nodes.begin(), parent_box->nodes.end(),
                             [&](const std::unique_ptr<MathNode> &n) {
                               return n.get() == active_box->parent_node;
                             });

      if (it != parent_box->nodes.end()) {
        int dist = std::distance(parent_box->nodes.begin(), it);
        if (active_box->parent_node->is_fraction() &&
            active_box == static_cast<MathFraction *>(active_box->parent_node)
                              ->numerator.get()) {
          active_box = static_cast<MathFraction *>(active_box->parent_node)
                           ->denominator.get();
          cursor_index = 0;
        } else if (active_box->nodes.empty()) {
          active_box = parent_box;
          cursor_index = dist;
          active_box->remove(cursor_index);
        } else {
          active_box = parent_box;
          cursor_index = dist + 1;
        }
      }
    }
  }
}

void MathEditor::move_left() {
  if (cursor_index > 0) {
    cursor_index--;
    auto &prev = active_box->nodes[cursor_index];
    if (prev->is_fraction()) {
      active_box = static_cast<MathFraction *>(prev.get())->denominator.get();
      cursor_index = active_box->nodes.size();
    } else if (prev->is_power()) {
      active_box = static_cast<MathPower *>(prev.get())->exponent.get();
      cursor_index = active_box->nodes.size();
    } else if (prev->is_sqrt()) {
      active_box = static_cast<MathSqrt *>(prev.get())->content.get();
      cursor_index = active_box->nodes.size();
    } else if (prev->is_integral()) {
      auto integ = static_cast<MathIntegral *>(prev.get());
      if (integ->upper) {
        active_box = integ->upper.get();
        cursor_index = active_box->nodes.size();
      } else if (integ->lower) {
        active_box = integ->lower.get();
        cursor_index = active_box->nodes.size();
      }
    }
  } else {
    if (active_box->parent_node) {
      auto parent_box = active_box->parent_node->parent_box;
      auto it = std::find_if(parent_box->nodes.begin(), parent_box->nodes.end(),
                             [&](const std::unique_ptr<MathNode> &n) {
                               return n.get() == active_box->parent_node;
                             });

      if (it != parent_box->nodes.end()) {
        int dist = std::distance(parent_box->nodes.begin(), it);
        auto pnode = active_box->parent_node;

        if (pnode->is_fraction()) {
          auto fr = static_cast<MathFraction *>(pnode);
          if (active_box == fr->denominator.get()) {
            active_box = fr->numerator.get();
            cursor_index = active_box->nodes.size();
          } else {
            active_box = parent_box;
            cursor_index = dist;
          }
        } else if (pnode->is_sqrt()) {
          active_box = parent_box;
          cursor_index = dist;
        } else if (pnode->is_integral()) {
          auto integ = static_cast<MathIntegral *>(pnode);
          if (active_box == integ->upper.get() && integ->lower) {
            active_box = integ->lower.get();
            cursor_index = active_box->nodes.size();
          } else {
            active_box = parent_box;
            cursor_index = dist;
          }
        } else {
          active_box = parent_box;
          cursor_index = dist;
        }
      }
    }
  }
}

void MathEditor::move_right() {
  if (cursor_index < (int)active_box->nodes.size()) {
    auto &next = active_box->nodes[cursor_index];
    if (next->is_fraction()) {
      active_box = static_cast<MathFraction *>(next.get())->numerator.get();
      cursor_index = 0;
    } else if (next->is_power()) {
      active_box = static_cast<MathPower *>(next.get())->exponent.get();
      cursor_index = 0;
    } else if (next->is_sqrt()) {
      active_box = static_cast<MathSqrt *>(next.get())->content.get();
      cursor_index = 0;
    } else if (next->is_integral()) {
      auto integ = static_cast<MathIntegral *>(next.get());
      if (integ->lower) {
        active_box = integ->lower.get();
        cursor_index = 0;
      } else if (integ->upper) {
        active_box = integ->upper.get();
        cursor_index = 0;
      } else {
        cursor_index++;
      }
    } else {
      cursor_index++;
    }
  } else {
    if (active_box->parent_node) {
      auto parent_box = active_box->parent_node->parent_box;
      auto it = std::find_if(parent_box->nodes.begin(), parent_box->nodes.end(),
                             [&](const std::unique_ptr<MathNode> &n) {
                               return n.get() == active_box->parent_node;
                             });

      if (it != parent_box->nodes.end()) {
        int dist = std::distance(parent_box->nodes.begin(), it);
        auto pnode = active_box->parent_node;

        if (pnode->is_fraction()) {
          auto fr = static_cast<MathFraction *>(pnode);
          if (active_box == fr->numerator.get()) {
            active_box = fr->denominator.get();
            cursor_index = 0;
          } else {
            active_box = parent_box;
            cursor_index = dist + 1;
          }
        } else if (pnode->is_sqrt()) {
          active_box = parent_box;
          cursor_index = dist + 1;
        } else if (pnode->is_integral()) {
          auto integ = static_cast<MathIntegral *>(pnode);
          if (active_box == integ->lower.get() && integ->upper) {
            active_box = integ->upper.get();
            cursor_index = 0;
          } else {
            active_box = parent_box;
            cursor_index = dist + 1;
          }
        } else {
          active_box = parent_box;
          cursor_index = dist + 1;
        }
      }
    }
  }
}

void MathEditor::measure_and_update_cursor(cairo_t *cr, double x, double y,
                                           double font_size) {
  (void)cr;
  (void)x;
  (void)y;

  // local font for height calculation
  double effective_font_size = font_size * active_box->last_metrics.scale;

  if (active_box->nodes.empty()) {
    cursor_x = active_box->last_x;
    cursor_y = active_box->last_y - effective_font_size * 0.7;
    cursor_height = effective_font_size * 0.9;
  } else if (cursor_index == 0) {
    auto &next = active_box->nodes[0];
    cursor_x = next->last_x;
    cursor_y = next->last_y - effective_font_size * 0.7;
    cursor_height = effective_font_size * 0.9;
  } else {
    auto &prev = active_box->nodes[cursor_index - 1];
    cursor_x = prev->last_x + prev->last_metrics.width;
    cursor_y = prev->last_y - effective_font_size * 0.7;
    cursor_height = effective_font_size * 0.9;
  }
}

static MathBox *hit_test(MathBox *box, double x, double y, int &idx) {
  // recurse kids
  for (size_t i = 0; i < box->nodes.size(); ++i) {
    auto &n = box->nodes[i];
    if (n->is_fraction()) {
      auto fr = static_cast<MathFraction *>(n.get());
      // hit test num den
      if (y < fr->last_y - 2) {
        auto b = hit_test(fr->numerator.get(), x, y, idx);
        if (b)
          return b;
      } else {
        auto b = hit_test(fr->denominator.get(), x, y, idx);
        if (b)
          return b;
      }
    } else if (n->is_power()) {
      auto p = static_cast<MathPower *>(n.get());
      if (x >= p->last_x - 2 && x <= p->last_x + p->last_metrics.width + 2) {
        auto b = hit_test(p->exponent.get(), x, y, idx);
        if (b)
          return b;
      }
    } else if (n->is_sqrt()) {
      auto s = static_cast<MathSqrt *>(n.get());
      if (x >= s->last_x && x <= s->last_x + s->last_metrics.width) {
        auto b = hit_test(s->content.get(), x, y, idx);
        if (b)
          return b;
      }
    } else if (n->is_integral()) {
      auto integ = static_cast<MathIntegral *>(n.get());
      if (integ->upper && y < integ->last_y - 5) {
        auto b = hit_test(integ->upper.get(), x, y, idx);
        if (b)
          return b;
      } else if (integ->lower && y > integ->last_y + 5) {
        auto b = hit_test(integ->lower.get(), x, y, idx);
        if (b)
          return b;
      }
    }
  }

  // no child hit find idx
  if (box->nodes.empty()) {
    idx = 0;
    return box;
  }

  double cur_x = box->last_x;
  for (size_t i = 0; i < box->nodes.size(); ++i) {
    double mid = cur_x + box->nodes[i]->last_metrics.width / 2.0;
    if (x < mid) {
      idx = i;
      return box;
    }
    cur_x += box->nodes[i]->last_metrics.width;
  }
  idx = box->nodes.size();
  return box;
}

void MathEditor::handle_click(double x, double y) {
  int new_idx = 0;
  MathBox *new_box = hit_test(&root, x, y, new_idx);
  if (new_box) {
    active_box = new_box;
    cursor_index = new_idx;
  }
}

void MathEditor::draw(cairo_t *cr, double x, double y, double font_size) {
  cairo_select_font_face(cr, "DejaVu Serif", CAIRO_FONT_SLANT_NORMAL,
                         CAIRO_FONT_WEIGHT_NORMAL);
  root.measure(cr, font_size);
  root.draw(cr, x, y, font_size);
  measure_and_update_cursor(cr, x, y, font_size);

  // blink cursor if needed
  static int blink_counter = 0;
  blink_counter++;
  if ((blink_counter / 20) % 2 == 0) {
    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
    cairo_set_line_width(cr, 1.5);
    cairo_move_to(cr, cursor_x, cursor_y); // baseline cursor_y adjusted
    cairo_line_to(cr, cursor_x, cursor_y + cursor_height);
    cairo_stroke(cr);
  }
}

std::string MathEditor::get_expression() const { return root.to_string(); }

void MathEditor::clear() {
  root.nodes.clear();
  active_box = &root;
  cursor_index = 0;
}
MathBox::~MathBox() = default;
MathFraction::~MathFraction() = default;
MathPower::~MathPower() = default;
MathIntegral::~MathIntegral() = default;
