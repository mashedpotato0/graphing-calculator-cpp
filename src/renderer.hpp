#pragma once
#include "ast.hpp"
#include "ast_ext.hpp"
#include <algorithm>
#include <cairo.h>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct RenderBox {
  double x = 0, y = 0;
  double width = 0;
  double ascent = 0;
  double descent = 0;
  double scale = 1.0;

  double height() const { return ascent + descent; }
};

class MathRenderer {
  cairo_t *cr;
  double base_font_size;

  struct NodeLayout {
    RenderBox box;
    std::vector<std::unique_ptr<NodeLayout>> children;
  };

public:
  MathRenderer(cairo_t *context, double font_size = 24.0)
      : cr(context), base_font_size(font_size) {
    // find serif font
    cairo_select_font_face(cr, "DejaVu Serif", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
  }

  void render(const expr &e, double x, double y) {
    auto layout = measure(e, base_font_size);
    draw(e, *layout, x, y);
  }

  RenderBox get_total_box(const expr &e) {
    return measure(e, base_font_size)->box;
  }

private:
  std::string get_symbol(const std::string &cmd) {
    if (cmd == "alpha")
      return "\u03B1";
    if (cmd == "beta")
      return "\u03B2";
    if (cmd == "gamma")
      return "\u03B3";
    if (cmd == "delta")
      return "\u03B4";
    if (cmd == "theta")
      return "\u03B8";
    if (cmd == "omega")
      return "\u03C9";
    if (cmd == "phi" || cmd == "varphi")
      return "\u03C6";
    if (cmd == "pi")
      return "\u03C0";
    if (cmd == "infty")
      return "\u221E";
    if (cmd == "zeta")
      return "\u03B6";
    if (cmd == "eta")
      return "\u03B7";
    if (cmd == "iota")
      return "\u03B9";
    if (cmd == "kappa")
      return "\u03BA";
    if (cmd == "lambda")
      return "\u03BB";
    if (cmd == "mu")
      return "\u03BC";
    if (cmd == "nu")
      return "\u03BD";
    if (cmd == "xi")
      return "\u03BE";
    if (cmd == "rho")
      return "\u03C1";
    if (cmd == "sigma")
      return "\u03C3";
    if (cmd == "tau")
      return "\u03C4";
    if (cmd == "upsilon")
      return "\u03C5";
    if (cmd == "chi")
      return "\u03C7";
    if (cmd == "psi")
      return "\u03C8";
    if (cmd == "sin" || cmd == "cos" || cmd == "tan" || cmd == "log" ||
        cmd == "ln" || cmd == "exp" || cmd == "sqrt")
      return cmd;
    return "\\" + cmd;
  }

  std::unique_ptr<NodeLayout> measure(const expr &e, double size) {
    auto layout = std::make_unique<NodeLayout>();
    layout->box.scale = size / base_font_size;
    cairo_set_font_size(cr, size);

    if (auto n = dynamic_cast<const number *>(&e)) {
      layout->box = measure_text(n->to_string(), size);
    } else if (auto v = dynamic_cast<const variable *>(&e)) {
      std::string text = v->name;
      if (text.size() > 1 && text[0] == '\\') {
        text = get_symbol(text.substr(1));
      }
      layout->box = measure_text(text, size);
    } else if (auto nc = dynamic_cast<const named_constant *>(&e)) {
      std::string text = nc->name;
      const auto &L = const_latex();
      auto it = L.find(text);
      if (it != L.end() && it->second.size() > 1 && it->second[0] == '\\') {
        text = get_symbol(it->second.substr(1));
      }
      layout->box = measure_text(text, size);
    } else if (auto a = dynamic_cast<const add *>(&e)) {
      if (!a->left || !a->right) {
        layout->box = measure_text("?", size);
        return layout;
      }
      auto l = measure(*a->left, size);
      auto r = measure(*a->right, size);
      auto op = measure_text("+", size);

      layout->box.width = l->box.width + op.width + r->box.width + 4;
      layout->box.ascent = std::max({l->box.ascent, op.ascent, r->box.ascent});
      layout->box.descent =
          std::max({l->box.descent, op.descent, r->box.descent});

      layout->children.push_back(std::move(l));
      layout->children.push_back(op_node("+", size));
      layout->children.push_back(std::move(r));
    } else if (auto m = dynamic_cast<const multiply *>(&e)) {
      if (!m->left || !m->right) {
        layout->box = measure_text("?", size);
        return layout;
      }
      auto l = measure(*m->left, size);
      auto r = measure(*m->right, size);

      layout->box.width = l->box.width + r->box.width + 2;
      layout->box.ascent = std::max(l->box.ascent, r->box.ascent);
      layout->box.descent = std::max(l->box.descent, r->box.descent);

      layout->children.push_back(std::move(l));
      layout->children.push_back(std::move(r));
    } else if (auto d = dynamic_cast<const divide *>(&e)) {
      if (!d->left || !d->right) {
        layout->box = measure_text("?", size);
        return layout;
      }
      auto num = measure(*d->left, size * 0.9);
      auto den = measure(*d->right, size * 0.9);

      layout->box.width = std::max(num->box.width, den->box.width) + 10;
      layout->box.ascent = num->box.height() + 4;
      layout->box.descent = den->box.height() + 4;

      layout->children.push_back(std::move(num));
      layout->children.push_back(std::move(den));
    } else if (auto p = dynamic_cast<const pow_node *>(&e)) {
      if (!p->base || !p->exponent) {
        layout->box = measure_text("?", size);
        return layout;
      }
      auto base = measure(*p->base, size);
      auto exp = measure(*p->exponent, size * 0.6);

      layout->box.width = base->box.width + exp->box.width + 1;
      layout->box.ascent = std::max(base->box.ascent,
                                    exp->box.height() + base->box.ascent * 0.5);
      layout->box.descent = base->box.descent;

      layout->children.push_back(std::move(base));
      layout->children.push_back(std::move(exp));
    } else if (auto f = dynamic_cast<const func_call *>(&e)) {
      if (f->name == "sqrt" && f->args.size() == 1) {
        auto arg = measure(*f->args[0], size);
        layout->box.width = arg->box.width + 15;
        layout->box.ascent = arg->box.ascent + 6;
        layout->box.descent = arg->box.descent + 2;
        layout->children.push_back(std::move(arg));
      } else {
        if (f->args.empty() || !f->args[0]) {
          layout->box = measure_text(f->name + "(?)", size);
          return layout;
        }
        auto name = measure_text(f->name, size);
        auto lp = measure_text("(", size);
        auto arg = measure(*f->args[0], size);
        auto rp = measure_text(")", size);
        layout->box.width =
            name.width + lp.width + arg->box.width + rp.width + 4;
        layout->box.ascent =
            std::max({name.ascent, lp.ascent, arg->box.ascent, rp.ascent});
        layout->box.descent =
            std::max({name.descent, lp.descent, arg->box.descent, rp.descent});
        layout->children.push_back(std::move(arg));
      }
    } else if (auto i = dynamic_cast<const integral *>(&e)) {
      auto sym = measure_text("\u222B", size * 1.5);
      std::unique_ptr<NodeLayout> lower = nullptr, upper = nullptr;
      if (i->lower)
        lower = measure(*i->lower, size * 0.6);
      if (i->upper)
        upper = measure(*i->upper, size * 0.6);

      number zero(0);
      auto integrand =
          i->integrand ? measure(*i->integrand, size) : measure(zero, size);
      auto var = measure_text("d" + i->var, size);

      double bounds_width = 0;
      if (lower)
        bounds_width = std::max(bounds_width, lower->box.width);
      if (upper)
        bounds_width = std::max(bounds_width, upper->box.width);

      layout->box.width =
          sym.width + bounds_width + integrand->box.width + var.width + 8;
      layout->box.ascent = std::max(sym.ascent, integrand->box.ascent);
      if (upper) {
        layout->box.ascent = std::max(layout->box.ascent,
                                      upper->box.height() + sym.ascent * 0.5);
      }

      layout->box.descent = std::max(sym.descent, integrand->box.descent);
      if (lower) {
        layout->box.descent = std::max(layout->box.descent,
                                       lower->box.height() + sym.descent * 0.5);
      }

      layout->children.resize(3); // lower upper integrand
      layout->children[0] = std::move(lower);
      layout->children[1] = std::move(upper);
      layout->children[2] = std::move(integrand);
    } else if (auto s = dynamic_cast<const sum_node *>(&e)) {
      auto sym = measure_text("\u2211", size * 1.3);
      std::unique_ptr<NodeLayout> lower =
          s->from ? measure(*s->from, size * 0.5) : nullptr;
      std::unique_ptr<NodeLayout> upper =
          s->to_expr ? measure(*s->to_expr, size * 0.5) : nullptr;
      auto body = s->body ? measure(*s->body, size) : op_node("?", size);

      double lbw = lower ? lower->box.width : 0;
      double ubw = upper ? upper->box.width : 0;
      double bdyw = body->box.width;

      layout->box.width = std::max({sym.width, lbw, ubw}) + bdyw + 10;
      layout->box.ascent = std::max(
          sym.ascent + (upper ? upper->box.height() + 2 : 0), body->box.ascent);
      layout->box.descent =
          std::max(sym.descent + (lower ? lower->box.height() + 2 : 0),
                   body->box.descent);

      layout->children.push_back(std::move(lower));
      layout->children.push_back(std::move(upper));
      layout->children.push_back(std::move(body));
    } else if (auto p = dynamic_cast<const product_node *>(&e)) {
      auto sym = measure_text("\u220F", size * 1.3);
      std::unique_ptr<NodeLayout> lower =
          p->from ? measure(*p->from, size * 0.5) : nullptr;
      std::unique_ptr<NodeLayout> upper =
          p->to_expr ? measure(*p->to_expr, size * 0.5) : nullptr;
      auto body = p->body ? measure(*p->body, size) : op_node("?", size);

      double lbw = lower ? lower->box.width : 0;
      double ubw = upper ? upper->box.width : 0;
      double bdyw = body->box.width;

      layout->box.width = std::max({sym.width, lbw, ubw}) + bdyw + 10;
      layout->box.ascent = std::max(
          sym.ascent + (upper ? upper->box.height() + 2 : 0), body->box.ascent);
      layout->box.descent =
          std::max(sym.descent + (lower ? lower->box.height() + 2 : 0),
                   body->box.descent);

      layout->children.push_back(std::move(lower));
      layout->children.push_back(std::move(upper));
      layout->children.push_back(std::move(body));
    } else if (auto l = dynamic_cast<const limit_node *>(&e)) {
      auto sym = measure_text("lim", size * 0.9);
      auto sub = measure_text(l->var + "\u2192", size * 0.5);
      auto to = l->to ? measure(*l->to, size * 0.5) : nullptr;
      auto body = l->body ? measure(*l->body, size) : op_node("?", size);

      double sub_w = sub.width + (to ? to->box.width : 0);
      layout->box.width = std::max(sym.width, sub_w) + body->box.width + 10;
      layout->box.ascent = std::max(sym.ascent, body->box.ascent);
      layout->box.descent =
          std::max(sym.descent + sub.ascent + (to ? to->box.height() : 0) + 2,
                   body->box.descent);

      layout->children.push_back(std::move(to));
      layout->children.push_back(std::move(body));
    } else if (auto dv = dynamic_cast<const deriv_node *>(&e)) {
      if (!dv->arg) {
        layout->box = measure_text("d/d?", size);
        return layout;
      }
      auto top = measure_text("d", size * 0.8);
      auto bot = measure_text("d" + dv->var, size * 0.8);
      auto arg = measure(*dv->arg, size);

      double dwidth = std::max(top.width, bot.width) + 4;
      layout->box.width = dwidth + arg->box.width + 6;
      layout->box.ascent = std::max(top.height() + 2.0, arg->box.ascent);
      layout->box.descent = std::max(bot.height() + 2.0, arg->box.descent);

      layout->children.push_back(std::move(arg));
    } else {
      layout->box = measure_text(e.to_string(), size);
    }

    return layout;
  }

  std::unique_ptr<NodeLayout> op_node(const std::string &txt, double size) {
    auto l = std::make_unique<NodeLayout>();
    l->box = measure_text(txt, size);
    return l;
  }

  void draw(const expr &e, const NodeLayout &layout, double x, double y) {
    cairo_set_font_size(cr, base_font_size * layout.box.scale);

    if (dynamic_cast<const number *>(&e)) {
      cairo_move_to(cr, x, y);
      cairo_show_text(cr, e.to_string().c_str());
    } else if (const auto *nc = dynamic_cast<const named_constant *>(&e)) {
      std::string text = nc->name;
      const auto &L = const_latex();
      auto it = L.find(text);
      if (it != L.end() && it->second.size() > 1 && it->second[0] == '\\') {
        text = get_symbol(it->second.substr(1));
      }
      cairo_move_to(cr, x, y);
      cairo_show_text(cr, text.c_str());
    } else if (dynamic_cast<const add *>(&e)) {
      if (layout.children.size() < 3)
        return;
      double cur_x = x;
      draw(*dynamic_cast<const add *>(&e)->left, *layout.children[0], cur_x, y);
      cur_x += layout.children[0]->box.width + 2;
      draw_text("+", cur_x, y, base_font_size * layout.box.scale);
      cur_x += layout.children[1]->box.width + 2;
      draw(*dynamic_cast<const add *>(&e)->right, *layout.children[2], cur_x,
           y);
    } else if (dynamic_cast<const multiply *>(&e)) {
      if (layout.children.size() < 2)
        return;
      double cur_x = x;
      draw(*dynamic_cast<const multiply *>(&e)->left, *layout.children[0],
           cur_x, y);
      cur_x += layout.children[0]->box.width + 1;
      draw(*dynamic_cast<const multiply *>(&e)->right, *layout.children[1],
           cur_x, y);
    } else if (auto d = dynamic_cast<const divide *>(&e)) {
      if (layout.children.size() < 2)
        return;
      double mid_x = x + layout.box.width / 2.0;
      draw(*d->left, *layout.children[0],
           mid_x - layout.children[0]->box.width / 2.0,
           y - 6 - layout.children[0]->box.descent);
      draw(*d->right, *layout.children[1],
           mid_x - layout.children[1]->box.width / 2.0,
           y + 6 + layout.children[1]->box.ascent);

      cairo_set_line_width(cr, 1.5);
      cairo_move_to(cr, x + 2, y - 4);
      cairo_line_to(cr, x + layout.box.width - 2, y - 4);
      cairo_stroke(cr);
    } else if (auto p = dynamic_cast<const pow_node *>(&e)) {
      if (layout.children.size() < 2)
        return;
      draw(*p->base, *layout.children[0], x, y);
      draw(*p->exponent, *layout.children[1], x + layout.children[0]->box.width,
           y - layout.children[0]->box.ascent * 0.7);
    } else if (auto i = dynamic_cast<const integral *>(&e)) {
      if (layout.children.size() < 3)
        return;
      double cur_x = x;
      double scale = layout.box.scale;
      auto sym_box = measure_text("\u222B", base_font_size * scale * 1.5);

      draw_text("\u222B", cur_x, y + 5 * scale, base_font_size * scale * 1.5);

      double bounds_x = cur_x + sym_box.width * 0.8;
      if (layout.children[1] && i->upper) { // upper
        draw(*i->upper, *layout.children[1], bounds_x,
             y - sym_box.ascent * 0.8);
      }
      if (layout.children[0] && i->lower) { // lower
        draw(*i->lower, *layout.children[0], bounds_x,
             y + sym_box.descent * 0.8);
      }

      double bounds_width = 0;
      if (layout.children[0])
        bounds_width = std::max(bounds_width, layout.children[0]->box.width);
      if (layout.children[1])
        bounds_width = std::max(bounds_width, layout.children[1]->box.width);

      cur_x += sym_box.width + bounds_width + 2 * scale;
      number zero(0);
      if (i->integrand) {
        draw(*i->integrand, *layout.children[2], cur_x, y);
      } else {
        draw(zero, *layout.children[2], cur_x, y);
      }
      cur_x += layout.children[2]->box.width + 4 * scale;

      draw_text("d" + i->var, cur_x, y, base_font_size * scale);
    } else if (auto s = dynamic_cast<const sum_node *>(&e)) {
      if (layout.children.size() < 3)
        return;
      double scale = layout.box.scale;
      auto sym_box = measure_text("\u2211", base_font_size * scale * 1.3);

      double lbw = layout.children[0] ? layout.children[0]->box.width : 0;
      double ubw = layout.children[1] ? layout.children[1]->box.width : 0;
      double max_bw = std::max({sym_box.width, lbw, ubw});

      double sym_x = x + (max_bw - sym_box.width) / 2.0;
      draw_text("\u2211", sym_x, y, base_font_size * scale * 1.3);

      double bounds_x_center = x + max_bw / 2.0;

      if (layout.children[1] && s->to_expr) {
        draw(*s->to_expr, *layout.children[1],
             bounds_x_center - layout.children[1]->box.width / 2.0,
             y - sym_box.ascent - 2);
      }
      if (layout.children[0] && s->from) {
        draw(*s->from, *layout.children[0],
             bounds_x_center - layout.children[0]->box.width / 2.0,
             y + sym_box.descent + layout.children[0]->box.ascent + 4);
      }

      if (layout.children[2] && s->body) {
        draw(*s->body, *layout.children[2], x + max_bw + 8, y);
      } else {
        draw_text("?", x + max_bw + 8, y, base_font_size * scale);
      }
    } else if (auto p = dynamic_cast<const product_node *>(&e)) {
      if (layout.children.size() < 3)
        return;
      double scale = layout.box.scale;
      auto sym_box = measure_text("\u220F", base_font_size * scale * 1.3);

      double lbw = layout.children[0] ? layout.children[0]->box.width : 0;
      double ubw = layout.children[1] ? layout.children[1]->box.width : 0;
      double max_bw = std::max({sym_box.width, lbw, ubw});

      double sym_x = x + (max_bw - sym_box.width) / 2.0;
      draw_text("\u220F", sym_x, y, base_font_size * scale * 1.3);

      double bounds_x_center = x + max_bw / 2.0;

      if (layout.children[1] && p->to_expr) {
        draw(*p->to_expr, *layout.children[1],
             bounds_x_center - layout.children[1]->box.width / 2.0,
             y - sym_box.ascent - 2);
      }
      if (layout.children[0] && p->from) {
        draw(*p->from, *layout.children[0],
             bounds_x_center - layout.children[0]->box.width / 2.0,
             y + sym_box.descent + layout.children[0]->box.ascent + 4);
      }

      if (layout.children[2] && p->body) {
        draw(*p->body, *layout.children[2], x + max_bw + 8, y);
      } else {
        draw_text("?", x + max_bw + 8, y, base_font_size * scale);
      }
    } else if (auto l = dynamic_cast<const limit_node *>(&e)) {
      if (layout.children.size() < 2)
        return;
      double scale = layout.box.scale;
      auto sym_box = measure_text("lim", base_font_size * scale * 0.9);
      draw_text("lim", x, y, base_font_size * scale * 0.9);

      auto sub = measure_text(l->var + "\u2192", base_font_size * scale * 0.5);
      double sub_x = x;
      draw_text(l->var + "\u2192", sub_x, y + sym_box.descent + sub.ascent,
                base_font_size * scale * 0.5);

      if (layout.children[0] && l->to) {
        draw(*l->to, *layout.children[0], sub_x + sub.width,
             y + sym_box.descent + sub.ascent);
      }

      double sym_w = std::max(
          sym_box.width,
          sub.width + (layout.children[0] ? layout.children[0]->box.width : 0));
      if (layout.children[1] && l->body) {
        draw(*l->body, *layout.children[1], x + sym_w + 8, y);
      } else {
        draw_text("?", x + sym_w + 8, y, base_font_size * scale);
      }
    } else if (auto f = dynamic_cast<const func_call *>(&e)) {
      if (layout.children.empty())
        return;
      if (f->name == "sqrt" && f->args.size() == 1) {
        double scale = layout.box.scale;
        double arg_x = x + 12;
        draw(*f->args[0], *layout.children[0], arg_x, y);

        cairo_set_line_width(cr, 1.5 * scale);
        cairo_move_to(cr, x + 2, y - layout.children[0]->box.ascent * 0.5);
        cairo_line_to(cr, x + 6, y + layout.children[0]->box.descent);
        cairo_line_to(cr, x + 10, y - layout.children[0]->box.ascent - 2);
        cairo_line_to(cr, x + layout.box.width - 2,
                      y - layout.children[0]->box.ascent - 2);
        cairo_stroke(cr);
      } else {
        double cur_x = x;
        draw_text(f->name, cur_x, y, base_font_size * layout.box.scale);
        cur_x +=
            measure_text(f->name, base_font_size * layout.box.scale).width + 1;
        draw_text("(", cur_x, y, base_font_size * layout.box.scale);
        cur_x += measure_text("(", base_font_size * layout.box.scale).width + 1;
        draw(*f->args[0], *layout.children[0], cur_x, y);
        cur_x += layout.children[0]->box.width + 1;
        draw_text(")", cur_x, y, base_font_size * layout.box.scale);
      }
    } else if (auto dv = dynamic_cast<const deriv_node *>(&e)) {
      if (layout.children.empty())
        return;
      double dw = layout.box.width - layout.children[0]->box.width - 6;
      draw_text(
          "d",
          x + (dw - measure_text("d", base_font_size * 0.8 * layout.box.scale)
                        .width) /
                  2.0,
          y - 8, base_font_size * 0.8 * layout.box.scale);
      draw_text("d" + dv->var,
                x + (dw - measure_text("d" + dv->var,
                                       base_font_size * 0.8 * layout.box.scale)
                              .width) /
                        2.0,
                y + 12, base_font_size * 0.8 * layout.box.scale);

      cairo_set_line_width(cr, 1.0);
      cairo_move_to(cr, x, y - 2);
      cairo_line_to(cr, x + dw, y - 2);
      cairo_stroke(cr);

      draw(*dv->arg, *layout.children[0], x + dw + 6, y);
    } else {
      cairo_move_to(cr, x, y);
      cairo_show_text(cr, e.to_string().c_str());
    }
  }

  RenderBox measure_text(const std::string &txt, double size) {
    cairo_set_font_size(cr, size);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, txt.c_str(), &ext);
    return {0,
            0,
            ext.x_advance,
            -ext.y_bearing,
            ext.height + ext.y_bearing,
            size / base_font_size};
  }

  void draw_text(const std::string &txt, double x, double y, double size) {
    cairo_set_font_size(cr, size);
    cairo_move_to(cr, x, y);
    cairo_show_text(cr, txt.c_str());
  }
};
