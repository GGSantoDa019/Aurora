#pragma once
class CurveEditor : public Component, public SettableTooltipClient, private Timer {
    Aurora &p;
    String kind;
    int index;
    Path path, grid, fill;
    std::unique_ptr<SurfaceView> surface;
    CurveShape shape;
    std::array<Point<float>, 8> handles{};
    std::array<float, 32> previous{};
    bool custom = false;
    int selected = -1;
    String id(const char *suffix) const {
        return key(kind == "env" ? "env" : kind == "filter" ? "filter" : "lfo", index, suffix);
    }
    void timerCallback() override {
        std::array<float, 32> values{};
        if (kind == "env") {
            values[0] = read(p, id("mode"));
            values[1] = read(p, id("count"));
            values[2] = read(p, id("tension"));
            for (int i = 0; i < 4; ++i)
                values[size_t(3 + i)] =
                    read(p, id(std::array<const char *, 4>{"a", "d", "s", "r"}[size_t(i)]));
            for (int i = 0; i < 8; ++i) {
                values[size_t(8 + i)] = read(p, id(("x" + String(i)).toRawUTF8()));
                values[size_t(16 + i)] = read(p, id(("y" + String(i)).toRawUTF8()));
            }
        } else if (kind == "filter") {
            values[0] = read(p, id("cutoff"));
            values[1] = read(p, id("res"));
            values[2] = read(p, id("type"));
        } else {
            values[0] = read(p, id("shape"));
            values[1] = read(p, id("count"));
            values[2] = read(p, id("tension"));
            for (int i = 0; i < 8; ++i) {
                values[size_t(8 + i)] = read(p, id(("x" + String(i)).toRawUTF8()));
                values[size_t(16 + i)] = read(p, id(("point" + String(i)).toRawUTF8()));
            }
        }
        if (values != previous) {
            previous = values;
            build();
            repaint();
        }
    }
    void build() {
        path.clear();
        fill.clear();
        float w = float(getWidth()), h = float(getHeight());
        if (w <= 0 || h <= 0)
            return;
        custom = kind == "env" ? previous[0] > .5f : kind == "lfo" && int(previous[0]) == 4;
        if (custom) {
            shape.count = juce::jlimit(2, 8, int(previous[1]));
            shape.tension = previous[2];
            for (int i = 0; i < 8; ++i) {
                shape.x[size_t(i)] = previous[size_t(8 + i)];
                shape.y[size_t(i)] = previous[size_t(16 + i)];
            }
            for (int i = 0; i < 128; ++i) {
                float x = i / 127.f, y = h * (1 - shape.at(x));
                if (!i)
                    path.startNewSubPath(0, y);
                else
                    path.lineTo(x * w, y);
            }
            for (int i = 0; i < shape.count; ++i)
                handles[size_t(i)] = {shape.x[size_t(i)] * w, (1 - shape.y[size_t(i)]) * h};
        } else if (kind == "env") {
            float a = previous[3], d = previous[4], s = previous[5], r = previous[6],
                  total = a + d + r + .4f;
            handles[0] = {a / total * w, 3};
            handles[1] = {(a + d) / total * w, h * (1 - s)};
            handles[2] = {(a + d + .4f) / total * w, h * (1 - s)};
            handles[3] = {w - 2, h - 2};
            path.startNewSubPath(0, h);
            for (int i = 0; i < 4; ++i)
                path.lineTo(handles[size_t(i)]);
        } else if (kind == "filter")
            for (int i = 0; i < 128; ++i) {
                float x = i / 127.f, f = 20 * std::pow(1000.f, x) / std::max(20.f, previous[0]),
                      k = 2 - 1.9f * previous[1],
                      den = std::sqrt((1 - f * f) * (1 - f * f) + k * k * f * f),
                      num = previous[2] < .5f    ? 1
                            : previous[2] < 1.5f ? f
                                                 : f * f;
                float y = h - 5 - std::min(1.2f, num / std::max(.001f, den)) * (h - 10) * .78f;
                if (!i)
                    path.startNewSubPath(0, y);
                else
                    path.lineTo(x * w, y);
            }
        else if (kind == "lfo")
            for (int i = 0; i < 128; ++i) {
                float x = i / 127.f;
                int mode = int(previous[0]);
                float y = mode == 0   ? std::sin(2 * pi * x)
                          : mode == 1 ? 1 - 4 * std::abs(x - .5f)
                          : mode == 2 ? 2 * x - 1
                          : x < .5f   ? 1
                                      : -1;
                float at = h * .5f - y * h * .42f;
                if (!i)
                    path.startNewSubPath(0, at);
                else
                    path.lineTo(x * w, at);
            }
        fill = path;
        fill.lineTo(w, h);
        fill.lineTo(0, h);
        fill.closeSubPath();
    }
    void commit() {
        write(p, id("count"), float(shape.count));
        for (int i = 0; i < shape.count; ++i) {
            write(p, id(("x" + String(i)).toRawUTF8()), shape.x[size_t(i)]);
            write(p, id(((kind == "env" ? "y" : "point") + String(i)).toRawUTF8()),
                  shape.y[size_t(i)]);
        }
        timerCallback();
    }

  public:
    CurveEditor(Aurora &a, String type, int instance) : p(a), kind(type), index(instance) {
        previous.fill(-100);
        if (kind == "osc") {
            surface = std::make_unique<SurfaceView>(p, index);
            addAndMakeVisible(*surface);
        } else
            timerCallback();
        setTooltip("Double-click: curve mode/add point. Right-click: remove interior point. Up to "
                   "8 points.");
        startTimerHz(30);
    }
    void resized() override {
        if (surface)
            surface->setBounds(getLocalBounds());
        grid.clear();
        for (int i = 1; i < 8; ++i) {
            grid.startNewSubPath(getWidth() * i / 8.f, 0);
            grid.lineTo(getWidth() * i / 8.f, float(getHeight()));
        }
        grid.startNewSubPath(0, getHeight() * .5f);
        grid.lineTo(float(getWidth()), getHeight() * .5f);
        build();
    }
    void paint(Graphics &g) override {
        g.fillAll(ink);
        g.setColour(line);
        g.strokePath(grid, PathStrokeType(.7f));
        if (kind == "env") {
            g.setColour(accent.withAlpha(.12f));
            g.fillPath(fill);
        }
        g.setColour(accent);
        g.strokePath(path, PathStrokeType(1.4f));
        if (custom || kind == "env") {
            int count = custom ? shape.count : 4;
            for (int i = 0; i < count; ++i)
                g.fillEllipse(handles[size_t(i)].x - 3, handles[size_t(i)].y - 3, 6, 6);
        }
    }
    void mouseDown(const MouseEvent &e) override {
        if (kind != "env" && kind != "lfo")
            return;
        if (kind == "lfo" && !custom) {
            write(p, id("shape"), 4);
            timerCallback();
        }
        selected = -1;
        float best = 1e9f;
        int count = custom ? shape.count : 4;
        for (int i = 0; i < count; ++i) {
            float distance = e.position.getDistanceFrom(handles[size_t(i)]);
            if (distance < best) {
                best = distance;
                selected = i;
            }
        }
        p.undo.beginNewTransaction("Edit curve");
        if (e.mods.isPopupMenu() && custom) {
            if (shape.count > 2 && selected > 0 && selected < shape.count - 1) {
                for (int i = selected; i < shape.count - 1; ++i) {
                    shape.x[size_t(i)] = shape.x[size_t(i + 1)];
                    shape.y[size_t(i)] = shape.y[size_t(i + 1)];
                }
                --shape.count;
                commit();
            }
            selected = -1;
            return;
        }
        mouseDrag(e);
    }
    void mouseDrag(const MouseEvent &e) override {
        if (selected < 0)
            return;
        float x = juce::jlimit(0.f, 1.f, e.position.x / std::max(1, getWidth())),
              y = juce::jlimit(0.f, 1.f, 1 - e.position.y / std::max(1, getHeight()));
        if (custom) {
            if (selected > 0 && selected < shape.count - 1) {
                float lo = shape.x[size_t(selected - 1)] + .005f,
                      hi = shape.x[size_t(selected + 1)] - .005f;
                if (lo < hi)
                    shape.x[size_t(selected)] = juce::jlimit(lo, hi, x);
            }
            shape.y[size_t(selected)] = y;
            commit();
        } else {
            float total = previous[3] + previous[4] + previous[6] + .4f;
            if (selected == 0)
                write(p, id("a"), std::max(.001f, x * total));
            else if (selected == 1) {
                write(p, id("d"), std::max(.001f, x * total - previous[3]));
                write(p, id("s"), y);
            } else if (selected == 2)
                write(p, id("s"), y);
            else
                write(p, id("r"), .005f + x * 3);
            timerCallback();
        }
    }
    void mouseDoubleClick(const MouseEvent &e) override {
        if (kind != "env" && kind != "lfo")
            return;
        write(p, id(kind == "env" ? "mode" : "shape"), kind == "env" ? 1 : 4);
        timerCallback();
        if (shape.count >= 8)
            return;
        float x = juce::jlimit(.001f, .999f, e.position.x / std::max(1, getWidth())),
              y = juce::jlimit(0.f, 1.f, 1 - e.position.y / std::max(1, getHeight()));
        int at = 1;
        while (at < shape.count - 1 && shape.x[size_t(at)] < x)
            ++at;
        for (int i = shape.count; i > at; --i) {
            shape.x[size_t(i)] = shape.x[size_t(i - 1)];
            shape.y[size_t(i)] = shape.y[size_t(i - 1)];
        }
        shape.x[size_t(at)] = x;
        shape.y[size_t(at)] = y;
        ++shape.count;
        commit();
    }
    int gpuState() const { return surface ? surface->gpuStatus.load() : 0; }
};
using Curve = CurveEditor;
