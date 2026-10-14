#pragma once
class PianoRoll : public Component {
    Aurora &p;
    std::array<std::atomic<float> *, 16> pitches{}, enabled{};

  public:
    explicit PianoRoll(Aurora &a) : p(a) {
        for (int i = 0; i < 16; ++i) {
            pitches[size_t(i)] = p.state.getRawParameterValue(key("step", i, "note"));
            enabled[size_t(i)] = p.state.getRawParameterValue(key("step", i, "on"));
        }
    }
    void paint(Graphics &g) override {
        g.fillAll(Colour(0xff0b1420));
        float w = getWidth() / 16.f, h = getHeight() / 24.f;
        for (int i = 0; i < 16; ++i) {
            for (int row = 0; row < 24; ++row) {
                g.setColour(Colour(0xff293544));
                g.drawRect(Rectangle<float>(i * w, row * h, w, h), .5f);
            }
            auto *on = enabled[size_t(i)];
            int note = int(pitches[size_t(i)]->load());
            if (on->load() > .5f && note >= 48 && note <= 71) {
                g.setColour(Colour(0xff26bdb7));
                g.fillRect(
                    Rectangle<float>(i * w + 1, (71 - note) * h + 1, w - 2, std::max(1.f, h - 2)));
            }
        }
    }
    void mouseDown(const MouseEvent &e) override {
        int step = juce::jlimit(0, 15, int(e.position.x * 16 / getWidth())),
            note = 71 - juce::jlimit(0, 23, int(e.position.y * 24 / getHeight()));
        auto *on = p.state.getParameter(key("step", step, "on"));
        auto *pitch = p.state.getParameter(key("step", step, "note"));
        pitch->beginChangeGesture();
        pitch->setValueNotifyingHost(pitch->convertTo0to1(float(note)));
        pitch->endChangeGesture();
        on->beginChangeGesture();
        on->setValueNotifyingHost(e.mods.isRightButtonDown() ? 0 : 1);
        on->endChangeGesture();
        repaint();
    }
};
class AdvancedEditor : public AudioProcessorEditor {
    Aurora &p;
    ComboBox section, instance;
    Label heading, status;
    TextButton sample{"Import sample"}, ir{"Import IR"}, save{"Save preset"}, load{"Load preset"};
    Viewport viewport;
    Component form;
    std::vector<std::unique_ptr<Component>> widgets;
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment>> sliders;
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment>> combos;
    std::vector<std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment>> buttons;
    std::unique_ptr<FileChooser> chooser;
    MidiKeyboardComponent keyboard;
    PianoRoll roll;

  public:
    AdvancedEditor(Aurora &a)
        : AudioProcessorEditor(a), p(a),
          keyboard(a.keyboard, MidiKeyboardComponent::horizontalKeyboard), roll(a) {
        setSize(840, 740);
        for (Component *c :
             std::initializer_list<Component *>{&section, &instance, &heading, &status, &sample,
                                                &ir, &save, &load, &viewport, &keyboard, &roll})
            addAndMakeVisible(c);
        heading.setText("AURORA  /  Modular Synth", dontSendNotification);
        heading.setFont(Font(24.f));
        status.setText("Three engines • 16 voices • MIDI input • stereo VST3",
                       dontSendNotification);
        section.addItemList({"Global", "Oscillator", "Envelope", "LFO", "Macro", "Filter",
                             "Routing", "FX rack", "Modulation", "Sequencer"},
                            1);
        section.onChange = [this] {
            populateInstances();
            rebuild();
            resized();
        };
        instance.onChange = [this] { rebuild(); };
        viewport.setViewedComponent(&form, false);
        section.setSelectedId(1, dontSendNotification);
        sample.onClick = [this] { chooseSample(); };
        ir.onClick = [this] { chooseIR(); };
        save.onClick = [this] { choosePreset(true); };
        load.onClick = [this] { choosePreset(false); };
        keyboard.setLowestVisibleKey(48);
        if (AURORA_FX) {
            heading.setText("AURORA FX / Stereo Processor", dontSendNotification);
            status.setText("Input stereo / racks 1–2 parallel / rack 3 main", dontSendNotification);
            section.clear(dontSendNotification);
            section.addItem("Global", 1);
            section.addItem("Filter", 6);
            section.addItem("Routing", 7);
            section.addItem("FX rack", 8);
            section.setSelectedId(8, dontSendNotification);
            keyboard.setVisible(false);
        }
        populateInstances();
        rebuild();
        resized();
    }
    void populateInstances() {
        int count = section.getSelectedId() == 2   ? 3
                    : section.getSelectedId() == 3 ? 4
                    : section.getSelectedId() == 4 ? 10
                    : section.getSelectedId() == 5 ? 8
                    : section.getSelectedId() == 6 ? 2
                    : section.getSelectedId() == 7 ? 7
                    : section.getSelectedId() == 8 ? 3
                    : section.getSelectedId() == 9 ? 64
                                                   : 1;
        instance.clear(dontSendNotification);
        for (int i = 0; i < count; ++i)
            instance.addItem(String(i + 1), i + 1);
        instance.setSelectedId(1, dontSendNotification);
    }
    void rebuild() {
        sliders.clear();
        combos.clear();
        buttons.clear();
        widgets.clear();
        int group = section.getSelectedId(), index = std::max(0, instance.getSelectedId() - 1),
            y = 8;
        for (auto *param : p.getParameters()) {
            auto *rp = dynamic_cast<RangedAudioParameter *>(param);
            if (!rp)
                continue;
            String id = rp->paramID, prefix;
            switch (group) {
            case 2:
                prefix = "osc" + String(index) + "_";
                break;
            case 3:
                prefix = "env" + String(index) + "_";
                break;
            case 4:
                prefix = "lfo" + String(index) + "_";
                break;
            case 5:
                prefix = "macro" + String(index) + "_";
                break;
            case 6:
                prefix = "filter" + String(index) + "_";
                break;
            case 7:
                prefix = "route" + String(index) + "_";
                break;
            case 8:
                prefix = "fx" + String(index) + "s";
                break;
            case 9:
                prefix = "mod" + String(index) + "_";
                break;
            }
            bool show = group == 1
                            ? (id == "master" || (!AURORA_FX && (id == "noise" || id == "sub")))
                        : group == 10 ? (id.startsWith("seq_") || id.startsWith("step"))
                                      : id.startsWith(prefix);
            if (!show)
                continue;
            auto label = std::make_unique<Label>();
            label->setText(rp->getName(120), dontSendNotification);
            label->setBounds(10, y, 355, 32);
            form.addAndMakeVisible(*label);
            widgets.push_back(std::move(label));
            if (auto *cp = dynamic_cast<AudioParameterChoice *>(rp)) {
                auto c = std::make_unique<ComboBox>();
                c->addItemList(cp->choices, 1);
                c->setBounds(375, y, 390, 30);
                form.addAndMakeVisible(*c);
                combos.push_back(std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(
                    p.state, id, *c));
                widgets.push_back(std::move(c));
            } else if (dynamic_cast<AudioParameterBool *>(rp)) {
                auto b = std::make_unique<ToggleButton>();
                b->setBounds(375, y, 390, 30);
                form.addAndMakeVisible(*b);
                buttons.push_back(std::make_unique<AudioProcessorValueTreeState::ButtonAttachment>(
                    p.state, id, *b));
                widgets.push_back(std::move(b));
            } else {
                auto s = std::make_unique<Slider>();
                s->setSliderStyle(Slider::LinearHorizontal);
                s->setTextBoxStyle(Slider::TextBoxRight, false, 90, 26);
                s->setBounds(375, y, 390, 30);
                form.addAndMakeVisible(*s);
                sliders.push_back(std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
                    p.state, id, *s));
                widgets.push_back(std::move(s));
            }
            y += 42;
        }
        form.setSize(780, std::max(y, viewport.getHeight()));
        sample.setEnabled(group == 2);
        ir.setEnabled(group == 8);
    }
    void chooseSample() {
        int slot = std::max(0, instance.getSelectedId() - 1);
        chooser = std::make_unique<FileChooser>("Import audio or SFZ zones (root C4 = MIDI 60)",
                                                File{}, "*.wav;*.aif;*.aiff;*.flac;*.sfz");
        chooser->launchAsync(
            FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [safe = Component::SafePointer<AdvancedEditor>(this), slot](const FileChooser &fc) {
                if (safe && fc.getResult().existsAsFile()) {
                    bool ok = fc.getResult().hasFileExtension("sfz")
                                  ? safe->p.loadSFZ(slot, fc.getResult())
                                  : safe->p.loadSample(slot, fc.getResult());
                    safe->status.setText(ok ? "Sample imported: " + fc.getResult().getFileName()
                                            : "Unsupported or oversized file",
                                         dontSendNotification);
                }
            });
    }
    void chooseIR() {
        int rack = std::max(0, instance.getSelectedId() - 1);
        chooser = std::make_unique<FileChooser>("Import IR for rack slot 1", File{},
                                                "*.wav;*.aif;*.flac");
        chooser->launchAsync(
            FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [safe = Component::SafePointer<AdvancedEditor>(this), rack](const FileChooser &fc) {
                if (safe && fc.getResult().existsAsFile()) {
                    safe->p.loadIR(rack, fc.getResult());
                    safe->status.setText("IR loaded into slot 1; choose Convolution",
                                         dontSendNotification);
                }
            });
    }
    void choosePreset(bool writing) {
        chooser = std::make_unique<FileChooser>(
            writing ? "Save Aurora preset" : "Load Aurora preset", File{}, "*.aurora");
        chooser->launchAsync(
            (writing ? FileBrowserComponent::saveMode : FileBrowserComponent::openMode) |
                FileBrowserComponent::canSelectFiles,
            [safe = Component::SafePointer<AdvancedEditor>(this), writing](const FileChooser &fc) {
                if (!safe || fc.getResult() == File{})
                    return;
                MemoryBlock b;
                if (writing) {
                    safe->p.getStateInformation(b);
                    bool ok = fc.getResult().withFileExtension("aurora").replaceWithData(
                        b.getData(), b.getSize());
                    safe->status.setText(ok ? "Preset saved" : "Could not save preset",
                                         dontSendNotification);
                } else if (fc.getResult().loadFileAsData(b)) {
                    safe->p.setStateInformation(b.getData(), int(b.getSize()));
                    safe->status.setText("Aurora preset loaded", dontSendNotification);
                }
            });
    }
    void paint(Graphics &g) override {
        g.fillAll(Colour(0xff131c29));
        g.setColour(Colour(0xff26bdb7));
        g.fillRect(0, 0, getWidth(), 4);
    }
    void resized() override {
        heading.setBounds(20, 15, 790, 40);
        section.setBounds(20, 68, 230, 32);
        instance.setBounds(265, 68, 80, 32);
        sample.setBounds(360, 68, 145, 32);
        ir.setBounds(520, 68, 130, 32);
        save.setBounds(20, 116, 130, 30);
        load.setBounds(165, 116, 130, 30);
        status.setBounds(310, 116, 505, 30);
        bool show = section.getSelectedId() == 10;
        roll.setVisible(show);
        int extra = show ? 150 : 0;
        roll.setBounds(20, getHeight() - 230, getWidth() - 40, 145);
        viewport.setBounds(20, 160, getWidth() - 40, getHeight() - 250 - extra);
        keyboard.setBounds(20, getHeight() - 75, getWidth() - 40, 60);
    }
};
