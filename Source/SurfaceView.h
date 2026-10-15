#pragma once
namespace aurora_ui {
class SurfaceView : public Component, private Timer, private OpenGLRenderer {
    Aurora &p;
    int slot;
    OpenGLContext context;
    std::unique_ptr<OpenGLShaderProgram> shader;
    DisplayQueue gpuQueue;
    DisplayFrame frame, gpuFrame;
    std::array<Path, 8> outlines, faces;
    std::array<float, 256> textureData{};
    GLuint buffer = 0, texture = 0;
    GLint attribute = -1, modeLocation = -1;
    std::atomic<int> width{1}, height{1};
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
        if (updated) {
            rebuild();
            gpuQueue.push(frame);
            context.triggerRepaint();
            repaint();
        }
    }
    void newOpenGLContextCreated() override {
        using namespace juce::gl;
        shader = std::make_unique<OpenGLShaderProgram>(context);
        const char *vertex = "#version 120\nattribute vec2 vertex; varying vec2 uv; void "
                             "main(){uv=(vertex+1.0)*0.5;gl_Position=vec4(vertex,0.0,1.0);}";
        const char *fragment = R"(#version 120
varying vec2 uv;uniform sampler2D waveform;uniform int engine;
void main(){vec3 colour=vec3(0.105,0.145,0.165);float grid=step(0.97,fract(uv.x*8.0))+step(0.98,fract(uv.y*4.0));colour+=grid*0.025;
for(int layer=7;layer>=0;--layer){float z=float(layer);float t=(uv.x-0.02-z*0.03)/0.75;
if(t>=0.0&&t<=1.0){float sample=texture2D(waveform,vec2(t,0.5)).r*2.0-1.0;float y=0.27+sample*0.2+z*0.065;
if(engine==0){float face=step(y,uv.y)*step(uv.y,y+0.065);colour=mix(colour,vec3(0.28+z*.02,0.43+z*.025,0.15),face*.38);float edge=1.0-smoothstep(.002,.006,abs(uv.y-y));colour=mix(colour,vec3(.67,.85,.31),edge*(layer==3?1.0:.65));}
else {float waveY=.5+sample*.4;float edge=1.0-smoothstep(.002,.007,abs(uv.y-waveY));float grain=engine==3?step(.08,fract(t*24.0)):1.0;colour=mix(colour,vec3(.67,.85,.31),edge*grain);}}}
gl_FragColor=vec4(colour,1.0);})";
        if (!shader->addVertexShader(vertex) || !shader->addFragmentShader(fragment) ||
            !shader->link()) {
            gpuStatus.store(-1);
            shader.reset();
            return;
        }
        attribute = glGetAttribLocation(shader->getProgramID(), "vertex");
        modeLocation = glGetUniformLocation(shader->getProgramID(), "engine");
        const float vertices[]{-1, -1, 1, -1, -1, 1, -1, 1, 1, -1, 1, 1};
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gpuStatus.store(1);
    }
    void renderOpenGL() override {
        using namespace juce::gl;
        OpenGLHelpers::clear(Colour(0xff1b252a));
        if (!shader)
            return;
        gpuQueue.latest(gpuFrame);
        for (int i = 0; i < 256; ++i)
            textureData[size_t(i)] =
                .5f + .5f * juce::jlimit(-1.f, 1.f, gpuFrame.samples[size_t(i)]);
        glViewport(0, 0, int(width.load() * context.getRenderingScale()),
                   int(height.load() * context.getRenderingScale()));
        shader->use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 256, 1, 0, GL_LUMINANCE, GL_FLOAT,
                     textureData.data());
        glUniform1i(glGetUniformLocation(shader->getProgramID(), "waveform"), 0);
        glUniform1i(modeLocation, gpuFrame.mode);
        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glEnableVertexAttribArray(GLuint(attribute));
        glVertexAttribPointer(GLuint(attribute), 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(GLuint(attribute));
    }
    void openGLContextClosing() override {
        using namespace juce::gl;
        if (buffer)
            glDeleteBuffers(1, &buffer);
        if (texture)
            glDeleteTextures(1, &texture);
        buffer = texture = 0;
        shader.reset();
    }

  public:
    std::atomic<int> gpuStatus{0};
    SurfaceView(Aurora &a, int index) : p(a), slot(index) {
        frame.mode = int(read(p, key("osc", slot, "mode")));
        frame.position = read(p, key("osc", slot, "position"));
        for (int i = 0; i < 256; ++i)
            frame.samples[size_t(i)] = p.displayWave(slot,i / 256., frame.position, 220, 48000);
        gpuQueue.push(frame);
        context.setRenderer(this);
        context.setComponentPaintingEnabled(false);
        context.setContinuousRepainting(false);
        context.attachTo(*this);
        startTimerHz(60);
    }
    ~SurfaceView() override {
        stopTimer();
        context.detach();
    }
    void resized() override {
        width.store(std::max(1, getWidth()));
        height.store(std::max(1, getHeight()));
        rebuild();
    }
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
