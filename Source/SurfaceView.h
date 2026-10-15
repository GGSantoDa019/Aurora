#pragma once
namespace aurora_ui {
class SurfaceView : public Component, private Timer {
    Aurora &p;
    int slot;
    DisplayFrame frame;
    std::array<Path,8> outlines,faces;
    void rebuild() {
        float w = float(getWidth()), h = float(getHeight());
        for (auto &path : outlines)
            path.clear();
        for (auto &face : faces)
            face.clear();
        for (int layer = 0; layer < 8; ++layer) {
            auto point = [&](int i, int z) {
                float x = i / 255.f * w * .75f + z * w * .03f + w * .02f,
                      y = h * .73f - frame.samples[size_t(i)] * h * .2f - z * h * .065f;
                return Point<float>(x, y);
            };
            for (int i = 0; i < 256; ++i) {
                auto v = point(i, layer);
                if (!i) {
                    outlines[size_t(layer)].startNewSubPath(v);
                    faces[size_t(layer)].startNewSubPath(v);
                } else {
                    outlines[size_t(layer)].lineTo(v);
                    faces[size_t(layer)].lineTo(v);
                }
            }
            for (int i = 255; i >= 0; --i)
                faces[size_t(layer)].lineTo(point(i, layer + 1));
            faces[size_t(layer)].closeSubPath();
        }
    }
    void timerCallback() override {
        if(!isShowing())return;
        DisplayFrame previous=frame;
        bool updated = p.displayFrames[size_t(slot)].latest(frame);
        int mode = int(read(p, key("osc", slot, "mode")));
        float position = read(p, key("osc", slot, "position"));
        if (!updated && (frame.mode != mode || frame.position != position)) {
            frame.mode = mode;
            frame.position = position;
            for (int i = 0; i < 256; ++i)
                frame.samples[size_t(i)] = p.displayWave(slot,i / 256., position, 220, 48000);
            updated = true;
        }
        if (updated && (previous.mode!=frame.mode || previous.position!=frame.position || previous.samples!=frame.samples)) {
            rebuild();
            repaint();
        }
    }
  public:
    std::atomic<int> gpuStatus{0};
    SurfaceView(Aurora &a, int index) : p(a), slot(index) {
        frame.mode = int(read(p, key("osc", slot, "mode")));
        frame.position = read(p, key("osc", slot, "position"));
        for (int i = 0; i < 256; ++i)
            frame.samples[size_t(i)] = p.displayWave(slot,i / 256., frame.position, 220, 48000);
        rebuild();
        startTimerHz(30);
    }
    ~SurfaceView() override { stopTimer(); }
    void resized() override { rebuild(); }
    void paint(Graphics &g) override {
        g.fillAll(ink);
        if (frame.mode == 0) {
            for (int layer = 7; layer >= 0; --layer) {
                g.setColour(waveGreen.withAlpha(.08f + layer * .014f));
                g.fillPath(faces[size_t(layer)]);
                g.setColour(waveGreen.withAlpha(layer == 3 ? 1.f : .22f + layer * .06f));
                g.strokePath(outlines[size_t(layer)], PathStrokeType(layer == 3 ? 1.5f : .8f));
            }
        } else {
            float w = float(getWidth()), h = float(getHeight());
            g.setColour(waveGreen);
            for (int i = 0; i < 255; ++i) {
                if (frame.mode == 3 && i % 12 < 2)
                    continue;
                g.drawLine(i * w / 255, h * .5f - frame.samples[size_t(i)] * h * .42f,
                           (i + 1) * w / 255, h * .5f - frame.samples[size_t(i + 1)] * h * .42f, 1);
                if (frame.mode == 3 && i % 12 == 0) {
                    g.setColour(waveGreen.withAlpha(.15f));
                    g.fillRect(i * w / 255, 0.f, w * 10 / 255, h);
                    g.setColour(waveGreen);
                }
            }
        }
    }
};
} // namespace aurora_ui
