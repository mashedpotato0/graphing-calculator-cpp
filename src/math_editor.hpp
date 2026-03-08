#pragma once

#include <cairo.h>
#include <memory>
#include <string>
#include <vector>

struct RenderMetrics {
  double width = 0;
  double ascent = 0;
  double descent = 0;
  double scale = 1.0;

  double height() const { return ascent + descent; }
};

class MathBox;

class MathNode {
public:
  virtual ~MathNode() = default;
  virtual RenderMetrics measure(cairo_t *cr, double font_size) = 0;
  virtual void draw(cairo_t *cr, double x, double y, double font_size) = 0;
  virtual std::string to_string() const = 0;

  double last_x = 0;
  double last_y = 0;
  RenderMetrics last_metrics;

  MathBox *parent_box = nullptr;

  virtual bool is_fraction() const { return false; }
  virtual bool is_power() const { return false; }
  virtual bool is_integral() const { return false; }
  virtual bool is_sqrt() const { return false; }
};

class MathBox {
public:
  std::vector<std::unique_ptr<MathNode>> nodes;
  MathNode *parent_node = nullptr;

  double last_x = 0;
  double last_y = 0;
  RenderMetrics last_metrics;

  ~MathBox();
  RenderMetrics measure(cairo_t *cr, double font_size);
  void draw(cairo_t *cr, double x, double y, double font_size);
  std::string to_string() const;
  void insert(int index, std::unique_ptr<MathNode> node);
  void remove(int index);
};

class MathGlyph : public MathNode {
public:
  std::string character;
  MathGlyph(std::string c) : character(std::move(c)) {}
  RenderMetrics measure(cairo_t *cr, double font_size) override;
  void draw(cairo_t *cr, double x, double y, double font_size) override;
  std::string to_string() const override { return character; }
};

class MathSymbol : public MathNode {
public:
  std::string display_text;
  std::string latex_command;
  MathSymbol(std::string disp, std::string latex)
      : display_text(std::move(disp)), latex_command(std::move(latex)) {}
  RenderMetrics measure(cairo_t *cr, double font_size) override;
  void draw(cairo_t *cr, double x, double y, double font_size) override;
  std::string to_string() const override { return latex_command; }
};

class MathFraction : public MathNode {
public:
  std::unique_ptr<MathBox> numerator;
  std::unique_ptr<MathBox> denominator;

  MathFraction();
  ~MathFraction() override;
  RenderMetrics measure(cairo_t *cr, double font_size) override;
  void draw(cairo_t *cr, double x, double y, double font_size) override;
  std::string to_string() const override;
  bool is_fraction() const override { return true; }
};

class MathPower : public MathNode {
public:
  std::unique_ptr<MathBox> exponent;

  MathPower();
  ~MathPower() override;
  RenderMetrics measure(cairo_t *cr, double font_size) override;
  void draw(cairo_t *cr, double x, double y, double font_size) override;
  std::string to_string() const override;
  bool is_power() const override { return true; }
};

class MathSqrt : public MathNode {
public:
  std::unique_ptr<MathBox> content;

  MathSqrt();
  ~MathSqrt() override;
  RenderMetrics measure(cairo_t *cr, double font_size) override;
  void draw(cairo_t *cr, double x, double y, double font_size) override;
  std::string to_string() const override;
  bool is_sqrt() const override { return true; }
};

class MathIntegral : public MathNode {
public:
  std::unique_ptr<MathBox> lower; // optional, nullptr if indefinite
  std::unique_ptr<MathBox> upper; // optional, nullptr if indefinite

  MathIntegral();
  ~MathIntegral() override;
  RenderMetrics measure(cairo_t *cr, double font_size) override;
  void draw(cairo_t *cr, double x, double y, double font_size) override;
  std::string to_string() const override;
  bool is_integral() const override { return true; }

  // Which box is currently active (0=lower, 1=upper, or nullptr if neither)
  MathBox *active_sub = nullptr;
};

class MathEditor {
public:
  MathBox root;
  MathBox *active_box;
  int cursor_index;

  double cursor_x = 0;
  double cursor_y = 0;
  double cursor_height = 0;

  MathEditor();

  void insert_char(const std::string &c);
  void insert_fraction();
  void insert_power();
  void insert_integral();
  void insert_sqrt();
  void backspace();
  void delete_forward();
  void move_left();
  void move_right();

  void draw(cairo_t *cr, double x, double y, double font_size = 24.0);
  void handle_click(double x, double y);
  std::string get_expression() const;
  void clear();

private:
  void measure_and_update_cursor(cairo_t *cr, double x, double y,
                                 double font_size);
};
