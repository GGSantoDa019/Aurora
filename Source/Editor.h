#pragma once
#include "AdvancedEditor.h"
#include "ReferenceLayout.h"

namespace aurora_ui {
static const Colour ink{0xff1b252a}, panel{0xff334149}, line{0xff11191e}, accent{0xff22b9e9},
    text{0xffe0e8eb}, muted{0xffa2b2bb}, waveGreen{0xffa7d957};
inline float read(Aurora &p, const String &id) {
    auto *value = p.state.getRawParameterValue(id);
    float plain=value?value->load():0;
    return id.startsWith("route")&&id.endsWith("_dest")?plain*.25f:plain;
}
inline void write(Aurora &p, const String &id, float plain) {
    if(id.startsWith("route")&&id.endsWith("_dest"))plain=RoutingDescriptor::plain(3,plain);
    if (auto *parameter = p.state.getParameter(id)) {
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(parameter->convertTo0to1(plain));
        parameter->endChangeGesture();
    }
}

} // namespace aurora_ui
#include "SurfaceView.h"
namespace aurora_ui {
class Theme : public LookAndFeel_V4 {
    std::array<Path, 257> arcs;

  public:
    Theme() {
        setColour(Label::textColourId, text);
        setColour(Slider::textBoxTextColourId, text);
        setColour(Slider::textBoxBackgroundColourId, Colours::transparentBlack);
        setColour(Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour(ComboBox::backgroundColourId, Colour(0xff26343b));
        setColour(ComboBox::outlineColourId, line);
        setColour(ComboBox::textColourId, text);
        setColour(TextButton::buttonColourId, Colour(0xff26343b));
        setColour(TextButton::textColourOffId, text);
        setColour(TextButton::buttonOnColourId, accent.darker(.6f));
        setColour(TextEditor::backgroundColourId, ink);
        setColour(TextEditor::textColourId, text);
        setColour(TextEditor::outlineColourId, line);
        setColour(TextEditor::focusedOutlineColourId, accent);
        setColour(ListBox::backgroundColourId, ink);
        for (int i = 1; i < 257; ++i)
            arcs[size_t(i)].addCentredArc(0, 0, 1, 1, 0, -2.4f, -2.4f + 4.8f * i / 256, true);
    }
    void drawRotarySlider(Graphics &g, int x, int y, int w, int h, float value, float, float,
                          Slider &) override {
        float radius = std::max(1.f, std::min(w, h) * .5f - 3), cx = x + w * .5f, cy = y + h * .5f;
        g.setColour(line);
        g.strokePath(arcs[256], PathStrokeType(1.5f),
                     AffineTransform::scale(radius).translated(cx, cy));
        int at = juce::jlimit(0, 256, int(value * 256));
        g.setColour(accent);
        g.strokePath(arcs[size_t(at)], PathStrokeType(1.5f),
                     AffineTransform::scale(radius).translated(cx, cy));
        g.setGradientFill(ColourGradient(Colour(0xff98a8af), cx - radius, cy - radius,
                                         Colour(0xff35474f), cx + radius, cy + radius, false));
        g.fillEllipse(cx - radius + 3, cy - radius + 3, (radius - 3) * 2, (radius - 3) * 2);
        g.setColour(Colour(0xff17242b));
        g.drawEllipse(cx - radius + 3, cy - radius + 3, (radius - 3) * 2, (radius - 3) * 2, 1.2f);
        float angle = -2.4f + 4.8f * value;
        g.setColour(text);
        g.drawLine(cx, cy, cx + std::sin(angle) * (radius - 5), cy - std::cos(angle) * (radius - 5),
                   1.8f);
    }
};

class LearnSlider : public Slider {
    Aurora &p;
    String id;

  public:
    LearnSlider(Aurora &a, String key) : p(a), id(std::move(key)) {}
    void mouseDown(const MouseEvent &e) override {
        if (e.mods.isPopupMenu()) {
            PopupMenu menu;
            menu.addItem(1, "MIDI learn: move a CC control");
            menu.addItem(2, "Remove MIDI mapping");
            menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this),
                               [safe = Component::SafePointer<LearnSlider>(this)](int result) {
                                   if (!safe)
                                       return;
                                   auto it = safe->p.indices.find(safe->id);
                                   if (it == safe->p.indices.end())
                                       return;
                                   if (result == 1)
                                       safe->p.midiLearn.store(it->second);
                                   if (result == 2)
                                       for (auto &map : safe->p.midiMappings)
                                           if (map.load() == it->second)
                                               map.store(-1);
                               });
        } else
            Slider::mouseDown(e);
    }
};
class Knob : public Component, public DragAndDropTarget {
    Aurora &p;
    String id;
    Label label;
    LearnSlider slider;
    Slider depthSlider;
    int routeIndex = -1, preferredRoute = -1;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> depthAttachment;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> attachment;
    int target;
    bool hovering = false;
    float modulation = 0, minimum = 0, maximum = 0;
    bool compact = false;

  public:
    Knob(Aurora &a, String key, String title, int destination = 0)
        : p(a), id(key), slider(a, key), target(destination) {
        addAndMakeVisible(label);
        addAndMakeVisible(slider);
        addChildComponent(depthSlider);
        depthSlider.setSliderStyle(Slider::LinearHorizontal);
        depthSlider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        depthSlider.setPopupDisplayEnabled(true, true, this);
        depthSlider.setTooltip("Modulation depth: left = negative, centre = zero, right = "
                               "positive. Base value and signed depth define the minimum/maximum.");
        label.setText(title, dontSendNotification);
        label.setJustificationType(Justification::centred);
        label.setFont(Font(11.f));
        slider.setSliderStyle(Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(Slider::TextBoxBelow, false, 78, 20);
        slider.setName(title);
        slider.setWantsKeyboardFocus(true);
        slider.setTooltip(title + " / right-click: MIDI learn");
        if (auto *parameter = p.state.getParameter(id))
            slider.setDoubleClickReturnValue(
                true, parameter->convertFrom0to1(parameter->getDefaultValue()));
        attachment =
            std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(p.state, id, slider);
        slider.setNumDecimalPlacesToDisplay(2);
        auto range = p.state.getParameter(id)->getNormalisableRange();
        bool percent = range.start >= 0 && range.end <= 1 && !id.endsWith("_dest");
        slider.textFromValueFunction = [key = id, percent](double value) {
            if (key.endsWith("_dest"))
                return String(value < .5   ? "Filter"
                              : value < 1.5  ? "Main"
                              : value < 2.5 ? "Direct"
                                            : "None");
            if (percent)
                return String(value * 100, 0) + " %";
            if (key.endsWith("_cutoff"))
                return String(value, 0) + " Hz";
            if (key.endsWith("_grain"))
                return String(value, 0) + " ms";
            if (key.startsWith("env"))
                return String(value, 2) + " s";
            return String(value, 2);
        };
        slider.valueFromTextFunction = [key = id, percent](const String &value) {
            if (key.endsWith("_dest")) {
                if (value.containsIgnoreCase("main"))
                    return 1.;
                if (value.containsIgnoreCase("direct"))
                    return 2.;
                if (value.containsIgnoreCase("none"))
                    return 3.;
            }
            return value.getDoubleValue() / (percent ? 100. : 1.);
        };
        slider.updateText();
    }
    bool isInterestedInDragSource(const SourceDetails &d) override {
        return target > 0 && d.description.isInt();
    }
    void itemDragEnter(const SourceDetails &) override {
        hovering = true;
        repaint();
    }
    void itemDragExit(const SourceDetails &) override {
        hovering = false;
        repaint();
    }
    void itemDropped(const SourceDetails &d) override {
        hovering = false;
        int source = int(d.description), slot = -1;
        for (int i = 0; i < 64; ++i) {
            int existing = int(read(p, key("mod", i, "source")));
            if (existing == source && int(read(p, key("mod", i, "target"))) == target) {
                slot = i;
                break;
            }
            if (slot < 0 && existing == 0)
                slot = i;
        }
        if (slot < 0)
            return;
        if (read(p, key("mod", slot, "source")) == 0) {
            p.undo.beginNewTransaction("Create modulation");
            write(p, key("mod", slot, "source"), float(source));
            write(p, key("mod", slot, "target"), float(target));
            float base = p.state.getParameter(id)->convertTo0to1(float(slider.getValue()));
            bool high = target >= 9 ? slider.getValue() > 1000.0 : base > .5f;
            write(p, key("mod", slot, "depth"), high ? -.75f : .5f);
        }
        preferredRoute = slot;
        refresh();
        repaint();
    }
    void refresh() {
        int found = -1;
        for (int i = 0; i < 64; ++i)
            if (target > 0 && int(read(p, key("mod", i, "target"))) == target &&
                read(p, key("mod", i, "source")) > 0)
                found = i;
        if (preferredRoute >= 0 && read(p, key("mod", preferredRoute, "source")) > 0 &&
            int(read(p, key("mod", preferredRoute, "target"))) == target)
            found = preferredRoute;
        if (found != routeIndex) {
            depthAttachment.reset();
            routeIndex = found;
            depthSlider.setVisible(found >= 0);
            if (found >= 0)
                depthAttachment = std::make_unique<AudioProcessorValueTreeState::SliderAttachment>(
                    p.state, key("mod", found, "depth"), depthSlider);
            resized();
        }
        if (found >= 0) {
            float depth = read(p, key("mod", found, "depth"));
            bool bipolar = read(p, key("mod", found, "source")) >= 8 &&
                           read(p, key("mod", found, "source")) < 18 &&
                           !p.importedLfoUnipolar.load();
            auto *param = p.state.getParameter(id);
            float base = float(slider.getValue());
            auto range = param->getNormalisableRange();
            auto valueAt = [&](float offset) {
                return jlimit(range.start, range.end,
                              target >= 9   ? base * std::pow(2.f, offset * 8)
                              : target == 7 ? base + offset * 24
                                            : base + offset);
            };
            float lo = valueAt(bipolar ? -std::abs(depth) : std::min(0.f, depth)),
                  hi = valueAt(bipolar ? std::abs(depth) : std::max(0.f, depth));
            depthSlider.setTooltip("Depth " + String(depth, 2) + " | Min " +
                                   param->getText(param->convertTo0to1(lo), 32) + " | Max " +
                                   param->getText(param->convertTo0to1(hi), 32));
            depthSlider.textFromValueFunction = [this](double amount) {
                return "Depth " + String(amount, 2) + " | " +
                       depthSlider.getTooltip().fromFirstOccurrenceOf("Min", true, false);
            };
            float nextMinimum = param->convertTo0to1(lo), nextMaximum = param->convertTo0to1(hi);
            if (std::abs(minimum - nextMinimum) > .001f ||
                std::abs(maximum - nextMaximum) > .001f) {
                minimum = nextMinimum;
                maximum = nextMaximum;
                repaint();
            }
        }
        float amount = 0;
        if (target)
            for (int i = 0; i < 64; ++i)
                if (int(read(p, key("mod", i, "target"))) == target &&
                    read(p, key("mod", i, "source")) > 0)
                    amount += std::abs(read(p, key("mod", i, "depth")));
        amount = std::min(1.f, amount);
        if (std::abs(modulation - amount) > .001f) {
            modulation = amount;
            repaint();
        }
    }
    void paint(Graphics &g) override {
        if (hovering) {
            g.setColour(accent.withAlpha(.15f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 7);
        }
    }
    void paintOverChildren(Graphics &g) override {
        if (routeIndex < 0)
            return;
        auto bounds = slider.getBounds().toFloat().reduced(2.f);
        if (!compact)
            bounds.removeFromBottom(20.f);
        float radius = std::min(bounds.getWidth(), bounds.getHeight()) * .5f;
        Path limits;
        limits.addCentredArc(bounds.getCentreX(), bounds.getCentreY(), radius, radius, 0,
                             -2.35f + minimum * 4.7f, -2.35f + maximum * 4.7f, true);
        g.setColour(waveGreen);
        g.strokePath(limits, PathStrokeType(2.f));
    }
    void resized() override {
        if (compact) {
            label.setBounds(0, getHeight() - 16, getWidth(), 16);
            slider.setBounds(0, 0, getWidth(), getHeight() - (routeIndex >= 0 ? 28 : 15));
            depthSlider.setBounds(3, getHeight() - 29, getWidth() - 6, 12);
            return;
        }
        label.setBounds(0, 0, getWidth(), 18);
        slider.setBounds(0, 18, getWidth(), getHeight() - (routeIndex >= 0 ? 33 : 18));
        depthSlider.setBounds(3, getHeight() - 15, getWidth() - 6, 12);
    }
    void setCompact() {
        compact = true;
        label.setFont(Font(9.f));
        slider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
        slider.setPopupDisplayEnabled(true, true, this);
        resized();
    }
};
class Choice : public Component {
    Label label;
    ComboBox box;
    std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    bool compact = false;

  public:
    Choice(Aurora &p, String id, String name) {
        addAndMakeVisible(label);
        addAndMakeVisible(box);
        label.setText(name, dontSendNotification);
        label.setFont(Font(11.f));
        box.setName(name);
        if (auto *choice = dynamic_cast<AudioParameterChoice *>(p.state.getParameter(id)))
            box.addItemList(choice->choices, 1);
        attachment =
            std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment>(p.state, id, box);
    }
    void resized() override {
        if (compact) {
            box.setBounds(getLocalBounds());
            return;
        }
        label.setBounds(0, 0, 80, getHeight());
        box.setBounds(82, 0, std::max(20, getWidth() - 82), getHeight());
    }
    void setCompact() {
        compact = true;
        label.setVisible(false);
        resized();
    }
};
class SourceButton : public TextButton {
    var description;

  public:
    SourceButton(String label, var source) : TextButton(label), description(source) {}
    void setSource(var value) { description = value; }
    void mouseDrag(const MouseEvent &e) override {
        if (e.getDistanceFromDragStart() > 4)
            if (auto *container = DragAndDropContainer::findParentDragContainerFor(this))
                if (!container->isDragAndDropActive())
                    container->startDragging(description, this);
    }
};
#include "CurveEditor.h"
// Uses the existing route parameters, so old presets and host automation retain
// their IDs. Buttons select audio sources, while ENV/LFO modulation stays on knobs.
class FilterRouting : public Component, private Timer {
    Aurora &p;
    int filter;
    ComboBox output;
    std::array<TextButton, 5> inputs;
    static constexpr std::array<int, 5> sources{4, 0, 1, 2, 3};
    bool receives(int source, int target) const {
        if (read(p, key("route", source, "dest")) >= .25f)
            return false;
        float balance = read(p, key("route", source, "balance"));
        return target == 0 ? balance < 99.9f : balance > -99.9f;
    }
    void timerCallback() override {
        int selected = juce::jlimit(1, 4, int(read(p, key("route", filter + 5, "dest")) * 4) + 1);
        output.setSelectedId(selected, dontSendNotification);
        bool wouldCycle = read(p, key("route", 6 - filter, "dest")) < .25f;
        output.setItemEnabled(1, !wouldCycle);
        output.setTooltip(wouldCycle ? "Other filter already feeds this filter; serial feedback is disabled."
                                     : "Filter output destination");
        for (int i = 0; i < 5; ++i)
            inputs[size_t(i)].setToggleState(receives(sources[size_t(i)], filter), dontSendNotification);
    }
  public:
    FilterRouting(Aurora &processor, int index) : p(processor), filter(index) {
        output.addItem("Filter " + String(2 - filter), 1);
        output.addItem("Main", 2);
        output.addItem("Direct", 3);
        output.addItem("None", 4);
        output.setName("Filter " + String(filter + 1) + " output");
        addAndMakeVisible(output);
        output.onChange = [this] {
            int selection = output.getSelectedId();
            if (selection == 1 && read(p, key("route", 6 - filter, "dest")) < .25f) {
                timerCallback();
                return;
            }
            write(p, key("route", filter + 5, "dest"), (selection - 1) * .25f);
        };
        const StringArray labels{"S", "A", "B", "C", "N"};
        const StringArray names{"Sub", "Oscillator A", "Oscillator B", "Oscillator C", "Noise"};
        for (int i = 0; i < 5; ++i) {
            auto &button = inputs[size_t(i)];
            button.setButtonText(labels[i]);
            button.setName(names[i] + " to Filter " + String(filter + 1));
            button.setTooltip(names[i] + " through this filter. Both selected: 50/50 split. "
                              "Last filter deselected: source returns to Main.");
            button.setColour(TextButton::textColourOnId, accent);
            button.setColour(TextButton::buttonOnColourId, accent.withAlpha(.22f));
            addAndMakeVisible(button);
            button.onClick = [this, i] { toggleSource(i); };
        }
        timerCallback();
        startTimerHz(30);
    }
    void selectOutputForTest(int item) { output.setSelectedId(item, sendNotificationSync); }
    void toggleSource(int button) {
        int source = sources[size_t(button)];
        bool on = !receives(source, filter), other = receives(source, 1 - filter);
        p.undo.beginNewTransaction("Filter source routing");
        write(p, key("route", source, "balance"), on && other ? 0.f
              : (on ? filter : 1 - filter) == 0 ? -100.f : 100.f);
        write(p, key("route", source, "dest"), on || other ? 0.f : .25f);
        timerCallback();
    }
    void resized() override {
        output.setBounds(filter == 0 ? 79 : 180, 1, 21, 24);
        auto row = getLocalBounds().removeFromBottom(21);
        for (int i = 0; i < 5; ++i) {
            int left = i * row.getWidth() / 5, right = (i + 1) * row.getWidth() / 5;
            inputs[size_t(i)].setBounds(left, row.getY(), right - left - 1, row.getHeight());
        }
    }
};
class Card : public Component, public DragAndDropTarget {
  public:
    Aurora &p;
    String title;
    int rack = -1, slot = -1;
    int compactKind = 0;
    std::unique_ptr<SourceButton> source;
    std::unique_ptr<Curve> curve;
    std::unique_ptr<FilterRouting> filterRouting;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Choice>> choices;
    std::vector<std::unique_ptr<TextButton>> actions;
    Card(Aurora &a, String label, int sourceId = 0) : p(a), title(label) {
        if (sourceId) {
            source = std::make_unique<SourceButton>(label, var(sourceId));
            addAndMakeVisible(*source);
        }
    }
    void addKnob(String id, String label, int destination = 0) {
        auto control = std::make_unique<Knob>(p, id, label, destination);
        addAndMakeVisible(*control);
        knobs.push_back(std::move(control));
    }
    void addChoice(String id, String label) {
        auto control = std::make_unique<Choice>(p, id, label);
        addAndMakeVisible(*control);
        choices.push_back(std::move(control));
    }
    void addCurve(String kind, int index) {
        curve = std::make_unique<Curve>(p, kind, index);
        addAndMakeVisible(*curve);
    }
    void addFilterRouting(int index) {
        filterRouting = std::make_unique<FilterRouting>(p, index);
        addAndMakeVisible(*filterRouting);
    }
    void button(String label, std::function<void()> click) {
        auto b = std::make_unique<TextButton>(label);
        b->onClick = std::move(click);
        addAndMakeVisible(*b);
        actions.push_back(std::move(b));
    }
    void enableFXDrag(int r, int s) {
        rack = r;
        slot = s;
        source = std::make_unique<SourceButton>(title, "fx:" + String(r) + ":" + String(s));
        addAndMakeVisible(*source);
    }
    bool isInterestedInDragSource(const SourceDetails &d) override {
        return rack >= 0 && d.description.toString().startsWith("fx:" + String(rack) + ":");
    }
    void itemDropped(const SourceDetails &d) override {
        int from = d.description.toString().fromLastOccurrenceOf(":", false, false).getIntValue();
        if (from == slot || from < 0 || from > 3)
            return;
        p.undo.beginNewTransaction("Reorder effect slots");
        for (auto suffix : {"type", "amount", "mix", "time"}) {
            String a = "fx" + String(rack) + "s" + String(from) + "_" + suffix,
                   b = "fx" + String(rack) + "s" + String(slot) + "_" + suffix;
            float x = read(p, a), y = read(p, b);
            write(p, a, y);
            write(p, b, x);
        }
    }
    void paint(Graphics &g) override {
        g.setColour(panel);
        g.fillRect(getLocalBounds());
        g.setColour(line);
        g.drawRect(getLocalBounds(), 1);
        g.setColour(Colour(0xff8e9fa8));
        g.fillRect(1, 1, getWidth() - 2, 21);
        g.setColour(compactKind == 1 ? waveGreen : accent);
        g.fillRect(5, 7, 5, 7);
        if(compactKind==6) {
            g.setColour(ink); g.fillRect(1,54,getWidth()-2,117);
            Path shape; for(int i=0;i<getWidth()-4;++i){
                float y=112.f+24.f*std::sin(float(i)*(title=="SUB"?.12f:1.71f));
                if(i==0)shape.startNewSubPath(float(i+2),y);else shape.lineTo(float(i+2),y);
            }
            g.setColour(waveGreen);g.strokePath(shape,PathStrokeType(1));
            g.setFont(9.f);g.drawText(title=="SUB"?"SINE / -1 OCT":"WHITE",2,28,getWidth()-4,20,Justification::centred);
        }
        if (!source) {
            g.setColour(Colour(0xff19252c));
            g.setFont(Font(10.f, Font::bold));
            g.drawText(title, 15, 1, getWidth() - 22, 21, Justification::centredLeft);
        }
    }
    void resized() override {
        if (compactKind) {
            const int w = getWidth(), h = getHeight();
            if (source)
                source->setBounds(14, 1, w - 18, 20);
            if (compactKind == 1) {
                choices[0]->setBounds(58, 1, 188, 24);
                choices[1]->setBounds(49, 269, 156, 22);
                curve->setBounds(1, 72, 268, 99);
                const Rectangle<int> positions[]{{2,199,44,53},{218,254,44,53},
                    {2,27,62,43},{218,199,44,53},{164,199,44,53},{66,199,44,53},
                    {115,199,44,53},{2,254,44,53},{82,27,62,43},{166,27,62,43}};
                for(size_t i=0;i<knobs.size();++i)knobs[i]->setBounds(positions[i]);
                actions[0]->setBounds(3,173,155,21);
                actions[1]->setBounds(160,173,107,21);
            } else if (compactKind == 2) {
                if(filterRouting)filterRouting->setBounds(1,0,w-2,193);
                if(!choices.empty())choices[0]->setBounds(3,27,w-6,23);
                if(curve)curve->setBounds(1,54,w-2,117);
                for(size_t i=0;i<knobs.size();++i)
                    knobs[i]->setBounds(8+int(i)%3*58,199+int(i)/3*55,50,53);
            } else if (compactKind == 3 || compactKind == 4) {
                int top = 25;
                if (compactKind == 4) {
                    for (size_t i = 0; i < choices.size(); ++i)
                        choices[i]->setBounds(5 + int(i) * (w - 10) / 2, 27, (w - 10) / 2 - 3, 23);
                    top = 55;
                }
                if (curve)
                    curve->setBounds(5, top, w - 10, h - top - 77);
                int available = w - 10 - (actions.empty() ? 0 : 100);
                int kw = available / int(std::max(size_t(1), knobs.size()));
                for (size_t i = 0; i < knobs.size(); ++i)
                    knobs[i]->setBounds(5 + int(i) * kw, h - 69, kw, 64);
                for (auto &button : actions)
                    button->setBounds(w - 94, h - 65, 88, 24);
            } else if(compactKind == 7) {
                choices[0]->setBounds(6,30,w-12,23);
                for(size_t i=0;i<knobs.size();++i)knobs[i]->setBounds(10+int(i)*66,65,64,62);
            } else if(compactKind == 6) {
                for(size_t i=0;i<knobs.size();++i)
                    knobs[i]->setBounds((w-50)/2, i==0?254:199,50,53);
            } else if (compactKind == 5) {
                int kw = (w - 10) / 2;
                for (size_t i = 0; i < knobs.size(); ++i)
                    knobs[i]->setBounds(5 + int(i) % 2 * kw, 24 + int(i) / 2 * 54, kw, 53);
            }
            return;
        }
        auto area = getLocalBounds().reduced(12);
        if (source)
            source->setBounds(area.removeFromTop(26));
        else
            area.removeFromTop(26);
        area.removeFromTop(6);
        if (curve) {
            curve->setBounds(area.removeFromTop(64));
            area.removeFromTop(8);
        }
        for (auto &c : choices) {
            c->setBounds(area.removeFromTop(29));
            area.removeFromTop(5);
        }
        int columns = std::max(1, area.getWidth() / 84),
            rows = (int(knobs.size()) + columns - 1) / columns;
        int rowHeight = std::min(
            103, std::max(78, (area.getHeight() - int(actions.size()) * 28) / std::max(1, rows)));
        int width = area.getWidth() / columns;
        for (size_t i = 0; i < knobs.size(); ++i)
            knobs[i]->setBounds(area.getX() + int(i) % columns * width,
                                area.getY() + int(i) / columns * rowHeight, width, rowHeight);
        int y = area.getY() + rows * rowHeight;
        for (auto &b : actions) {
            b->setBounds(area.getX(), y, area.getWidth(), 25);
            y += 28;
        }
    }
    void makeCompact(int kind) {
        compactKind = kind;
        for (auto &knob : knobs)
            knob->setCompact();
        for (auto &choice : choices)
            choice->setCompact();
        resized();
    }
};
class Scope : public Component, public SettableTooltipClient, private Timer {
    WaveTap &tap;
    Path path;
    std::array<float, 512> waveform{}, incoming{};
    float peak = 0;
    void timerCallback() override {
        int count = tap.pull(incoming.data(), 512);
        if (count) {
            if (count < 512)
                std::move(waveform.begin() + count, waveform.end(), waveform.begin());
            std::copy_n(incoming.begin(), count, waveform.end() - count);
            path.clear();
            float w = float(getWidth() - 85), h = float(getHeight());
            for (int i = 0; i < 512; ++i) {
                float x = i * w / 511,
                      y = h * .5f - juce::jlimit(-1.f, 1.f, waveform[size_t(i)]) * h * .4f;
                if (!i)
                    path.startNewSubPath(x, y);
                else
                    path.lineTo(x, y);
            }
        }
        peak = std::max(tap.peak.load(), peak * .87f);
        repaint();
    }

  public:
    Scope(WaveTap &t) : tap(t) {
        startTimerHz(30);
        setTooltip("Output waveform and peak / FIFO from DSP");
    }
    void paint(Graphics &g) override {
        if(getWidth()<100){g.fillAll(ink);g.setColour(peak>1?Colours::orange:accent);
            int h=int((getHeight()-16)*std::min(1.f,peak));g.fillRect(10,getHeight()-4-h,getWidth()-20,h);
            g.setColour(text);g.setFont(8.f);g.drawText("OUT",0,0,getWidth(),12,Justification::centred);return;}
        g.setColour(line);
        g.drawHorizontalLine(getHeight() / 2, 0, float(getWidth() - 85));
        g.setColour(accent);
        g.strokePath(path, PathStrokeType(1));
        g.setColour(line);
        g.fillRect(getWidth() - 70, 12, 60, getHeight() - 24);
        g.setColour(peak > 1 ? Colours::orange : accent);
        g.fillRect(getWidth() - 70, 12, int(60 * std::min(1.f, peak)), getHeight() - 24);
    }
};
class MatrixModel : public ListBoxModel, private Timer {
    Aurora &p;
    ListBox &list;
    std::array<String, 64> sources, targets, depths, curves;
    void timerCallback() override {
        auto *sp = dynamic_cast<AudioParameterChoice *>(p.state.getParameter("mod0_source"));
        auto *tp = dynamic_cast<AudioParameterChoice *>(p.state.getParameter("mod0_target"));
        for (int i = 0; i < 64; ++i) {
            sources[size_t(i)] = sp->choices[juce::jlimit(0, sp->choices.size() - 1,
                                                          int(read(p, key("mod", i, "source"))))];
            targets[size_t(i)] = tp->choices[juce::jlimit(0, tp->choices.size() - 1,
                                                          int(read(p, key("mod", i, "target"))))];
            depths[size_t(i)] = String(read(p, key("mod", i, "depth")), 2);
            curves[size_t(i)] = String(read(p, key("mod", i, "curve")), 2);
        }
        list.repaint();
    }

  public:
    std::function<void(int)> selected;
    MatrixModel(Aurora &a, ListBox &l) : p(a), list(l) {
        timerCallback();
        startTimerHz(10);
    }
    int getNumRows() override { return 64; }
    void paintListBoxItem(int row, Graphics &g, int w, int h, bool active) override {
        if (row < 0 || row >= 64)
            return;
        g.fillAll(active ? accent.withAlpha(.13f) : row % 2 ? panel : ink);
        g.setColour(active ? accent : text);
        g.setFont(Font(12.f));
        g.drawText(sources[size_t(row)], 12, 0, w / 3 - 12, h, Justification::centredLeft);
        g.setColour(muted);
        g.drawText(targets[size_t(row)], w / 3, 0, w / 2, h, Justification::centredLeft);
        g.drawText(depths[size_t(row)], w - 140, 0, 65, h, Justification::centredRight);
        g.drawText(curves[size_t(row)], w - 65, 0, 55, h, Justification::centredRight);
    }
    void selectedRowsChanged(int row) override {
        if (selected)
            selected(row);
    }
};
} // namespace aurora_ui

using aurora_ui::read;
using aurora_ui::write;
class SynthPage : public Component {
  public:
    String pageName;
    explicit SynthPage(String name) : pageName(name) { setName(name); }
};
class OscPage : public SynthPage {
  public:
    OscPage() : SynthPage("OSC") {}
};
class MixPage : public SynthPage {
  public:
    MixPage() : SynthPage("MIX") {}
};
class FXPage : public SynthPage {
  public:
    FXPage() : SynthPage("FX") {}
};
class MatrixPage : public SynthPage {
  public:
    MatrixPage() : SynthPage("MATRIX") {}
};
class GlobalPage : public SynthPage {
  public:
    GlobalPage() : SynthPage("GLOBAL") {}
};
class PresetsPage : public SynthPage {
  public:
    PresetsPage() : SynthPage("PRESETS") {}
};
class PageHost : public Component {
    std::array<std::unique_ptr<SynthPage>, 6> pages;
    int active = 0;

  public:
    PageHost() {
        pages[0] = std::make_unique<OscPage>();
        pages[1] = std::make_unique<MixPage>();
        pages[2] = std::make_unique<FXPage>();
        pages[3] = std::make_unique<MatrixPage>();
        pages[4] = std::make_unique<GlobalPage>();
        pages[5] = std::make_unique<PresetsPage>();
        for (auto &page : pages)
            Component::addAndMakeVisible(*page);
        activate(0);
    }
    void activate(int page) {
        active = juce::jlimit(0, 5, page);
        for (int i = 0; i < 6; ++i)
            pages[size_t(i)]->setVisible(i == active);
    }
    void addAndMakeVisible(Component &child) { pages[size_t(active)]->addAndMakeVisible(child); }
    void resized() override {
        for (auto &page : pages)
            page->setBounds(getLocalBounds());
    }
};
class Editor : public AudioProcessorEditor, public DragAndDropContainer, private Timer {
    using Card = aurora_ui::Card;
    Aurora &p;
    aurora_ui::Theme theme;
    Label brand, preset, status;
    TextButton save{"Save"}, load{"Load"}, undo{"Undo"}, redo{"Redo"}, advanced{"All parameters"};
    std::array<TextButton, 5> tabs;
    std::array<TextButton,2> filterTabs;
    int selectedFilter=0;
    std::unique_ptr<aurora_ui::Knob> masterControl;
    Viewport viewport;
    PageHost content;
    MidiKeyboardComponent keyboard;
    Slider pitchWheel, modWheel;
    Label pitchCaption, modCaption;
    aurora_ui::Scope scope;
    std::vector<std::unique_ptr<Card>> cards;
    std::vector<std::unique_ptr<aurora_ui::SourceButton>> sources;
    std::vector<std::unique_ptr<aurora_ui::SourceButton>> envDock, lfoDock;
    std::unique_ptr<PianoRoll> roll;
    std::unique_ptr<AdvancedEditor> advancedEditor;
    ListBox matrix;
    aurora_ui::MatrixModel matrixModel;
    ComboBox modulationPicker;
    aurora_ui::SourceButton modulationDrag{"DRAG SOURCE", 8};
    int page = 0, envIndex = 0, lfoIndex = 0, rackIndex = 2, modIndex = 0;
    ComboBox selector, lfoSelector;
    std::unique_ptr<FileChooser> chooser;
    struct BrowserState {
        CriticalSection lock;
        Array<File> files;
        String message, presetName, importReport;
        std::atomic<bool> ready{false};
    };
    std::shared_ptr<BrowserState> browser = std::make_shared<BrowserState>();
    ThreadPool jobs{1};
    ListBox browserList;
    TextEditor search;
    TextButton folder{"Choose preset folder"};
    TextButton presetsButton{"Presets"};
    struct BrowserModel : ListBoxModel {
        Editor &owner;
        Array<File> files;
        StringArray names;
        BrowserModel(Editor &e) : owner(e) {}
        int getNumRows() override { return names.size(); }
        void paintListBoxItem(int row, Graphics &g, int w, int h, bool active) override {
            g.fillAll(active    ? aurora_ui::accent.withAlpha(.15f)
                      : row % 2 ? aurora_ui::panel
                                : aurora_ui::ink);
            g.setColour(aurora_ui::text);
            if (row >= 0 && row < names.size())
                g.drawText(names[row], 12, 0, w - 24, h, Justification::centredLeft);
        }
        void listBoxItemDoubleClicked(int row, const MouseEvent &) override {
            if (row >= 0 && row < files.size())
                owner.loadFile(files[row]);
        }
    } browserModel;

  public:
    void refreshForTest() { timerCallback(); }
    bool referenceLayoutForTest() {
        showPage(0);
        bool okay=cards.size()==11;
        for(int i=0;i<3;++i)okay &= cards[size_t(i)]->getBounds()==Rectangle<int>(76+270*i,0,270,318);
        for(int f=0;f<2;++f){filterTabs[size_t(f)].onClick();
            okay &= cards[size_t(3+f)]->isVisible() && !cards[size_t(4-f)]->isVisible();}
        filterTabs[0].onClick();
        for(auto &c:cards)for(auto &k:c->knobs)
            okay &= c->getLocalBounds().contains(k->getBounds());
        for(int width:{660,960,1190,1600}){
            setSize(width,roundToInt(width*740.0/1190.0));okay &= pageFitsForTest();
        }
        setSize(960,roundToInt(960*740.0/1190.0));
        return okay;
    }

    bool pageFitsForTest() {
        auto bounds = content.getBoundsInParent();
        return bounds.getBottom() <= viewport.getHeight() + 1 &&
               bounds.getRight() <= viewport.getWidth() + 1 &&
               !viewport.getVerticalScrollBar().isVisible() &&
               !viewport.getHorizontalScrollBar().isVisible() &&
               keyboard.getBoundsInParent().getBottom() <= getHeight() + 1;
    }
    int testGPU() {
#if JUCE_MODAL_LOOPS_PERMITTED
        showPage(0);
        setBounds(-5000, -5000, 1320, 780);
        addToDesktop(ComponentPeer::windowIsTemporary);
        setVisible(true);
        for (int i = 0; i < 12; ++i)
            MessageManager::getInstance()->runDispatchLoopUntil(40);
        int status = cards[0]->curve->gpuState();
        removeFromDesktop();
        return status;
#else
        return 0;
#endif
    }
    explicit Editor(Aurora &a)
        : AudioProcessorEditor(a), p(a),
          keyboard(a.keyboard, MidiKeyboardComponent::horizontalKeyboard), scope(a.telemetry),
          matrixModel(a, matrix), browserModel(*this) {
        setLookAndFeel(&theme);
        masterControl=std::make_unique<aurora_ui::Knob>(p,"master","MAIN");
        masterControl->setCompact();addAndMakeVisible(*masterControl);
        for(int i=0;i<2;++i){auto &b=filterTabs[size_t(i)];
            b.setButtonText("FILTER "+String(i+1));addAndMakeVisible(b);
            b.onClick=[this,i]{selectedFilter=i;layoutPage();};}

        for (int i = 0; i < 4; ++i) {
            auto button = std::make_unique<aurora_ui::SourceButton>("ENV " + String(i + 1), 4 + i);
            button->onClick = [this, i] {
                envIndex = i;
                showPage(0);
            };
            addAndMakeVisible(*button);
            envDock.push_back(std::move(button));
        }
        for (int i = 0; i < 10; ++i) {
            auto button = std::make_unique<aurora_ui::SourceButton>(String(i + 1), 8 + i);
            button->setTooltip("LFO " + String(i + 1) + " / drag to modulate");
            button->onClick = [this, i] {
                lfoIndex = i;
                showPage(0);
            };
            addAndMakeVisible(*button);
            lfoDock.push_back(std::move(button));
        }
        addAndMakeVisible(presetsButton);
        presetsButton.onClick = [this] { showPage(5); };
        setResizable(true, true);
        setResizeLimits(660, 390, 1800, 1064);
        if (auto *limits = getConstrainer())
            limits->setFixedAspectRatio(1190.0 / 740.0);
        for (Component *c :
             std::initializer_list<Component *>{&brand, &preset, &status, &save, &load, &undo,
                                                &redo, &advanced, &viewport, &keyboard, &scope})
            addAndMakeVisible(c);
        brand.setText(AURORA_FX ? "AURORA FX" : "AURORA", dontSendNotification);
        brand.setFont(Font(19.f, Font::bold));
        preset.setText("Init / Independent Audio", dontSendNotification);
        preset.setFont(Font(14.f));
        status.setText(
            "Drag a modulation source onto a highlighted knob. Right-click a knob for MIDI learn.",
            dontSendNotification);
        status.setFont(Font(11.f));
        status.setColour(Label::textColourId, aurora_ui::muted);
        StringArray titles{"OSC", "MIX", "FX", "MATRIX", "GLOBAL"};
        for (int i = 0; i < 5; ++i) {
            tabs[size_t(i)].setButtonText(titles[i]);
            tabs[size_t(i)].setClickingTogglesState(false);
            tabs[size_t(i)].onClick = [this, i] { showPage(i); };
            addAndMakeVisible(tabs[size_t(i)]);
            if (AURORA_FX && (i == 0 || i == 3 || i == 4))
                tabs[size_t(i)].setEnabled(false);
        }
        viewport.setViewedComponent(&content, false);
        keyboard.setLowestVisibleKey(48);
        keyboard.setVisible(!AURORA_FX);
        for (auto *wheel : {&pitchWheel, &modWheel}) {
            addAndMakeVisible(*wheel);
            wheel->setSliderStyle(Slider::LinearHorizontal);
            wheel->setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
            wheel->setVisible(!AURORA_FX);
        }
        pitchWheel.setName("Pitch wheel");
        pitchWheel.setTooltip("Pitch bend / release returns to center");
        pitchWheel.setRange(0, 16383, 1);
        pitchWheel.setValue(8192, dontSendNotification);
        pitchWheel.onValueChange = [this] { p.uiPitch.store(int(pitchWheel.getValue())); };
        pitchWheel.onDragEnd = [this] { pitchWheel.setValue(8192); };
        modWheel.setName("Mod wheel");
        modWheel.setTooltip("CC 1 / choose Mod wheel as a modulation source");
        modWheel.setRange(0, 127, 1);
        modWheel.onValueChange = [this] { p.uiMod.store(int(modWheel.getValue())); };
        addAndMakeVisible(pitchCaption);
        addAndMakeVisible(modCaption);
        pitchCaption.setText("Pitch", dontSendNotification);
        modCaption.setText("Mod", dontSendNotification);
        pitchCaption.setVisible(!AURORA_FX);
        modCaption.setVisible(!AURORA_FX);
        save.onClick = [this] { choosePreset(true); };
        load.onClick = [this] { choosePreset(false); };
        undo.onClick = [this] { p.undo.undo(); };
        redo.onClick = [this] { p.undo.redo(); };
        advanced.onClick = [this] {
            if (!advancedEditor) {
                advancedEditor = std::make_unique<AdvancedEditor>(p);
                addAndMakeVisible(*advancedEditor);
            } else
                advancedEditor->setVisible(!advancedEditor->isVisible());
            if (advancedEditor->isVisible())
                advancedEditor->toFront(false);
            resized();
        };
        matrix.setModel(&matrixModel);
        matrix.setRowHeight(28);
        matrixModel.selected = [this](int row) {
            if (row >= 0) {
                modIndex = row;
                MessageManager::callAsync([safe = Component::SafePointer<Editor>(this)] {
                    if (safe && safe->page == 3)
                        safe->buildModControls();
                });
            }
        };
        browserList.setModel(&browserModel);
        browserList.setRowHeight(34);
        search.setTextToShowWhenEmpty("Search presets", aurora_ui::muted);
        search.onTextChange = [this] { filterPresets(); };
        folder.onClick = [this] { chooseFolder(); };
        selector.onChange = [this] {
            int index = std::max(0, selector.getSelectedId() - 1);
            if (page == 0)
                envIndex = index;
            else if (page == 3)
                lfoIndex = index;
            else if (page == 2)
                rackIndex = index;
            MessageManager::callAsync([safe = Component::SafePointer<Editor>(this)] {
                if (safe)
                    safe->showPage(safe->page);
            });
        };
        addAndMakeVisible(modulationPicker);
        addAndMakeVisible(modulationDrag);
        if (auto *choices =
                dynamic_cast<AudioParameterChoice *>(p.state.getParameter("mod0_source")))
            for (int i = 1; i < choices->choices.size(); ++i)
                modulationPicker.addItem(choices->choices[i], i);
        modulationPicker.setSelectedId(8, dontSendNotification);
        modulationPicker.onChange = [this] {
            modulationDrag.setSource(modulationPicker.getSelectedId());
        };
        modulationPicker.setVisible(!AURORA_FX);
        modulationDrag.setVisible(!AURORA_FX);
        lfoSelector.onChange = [this] {
            lfoIndex = std::max(0, lfoSelector.getSelectedId() - 1);
            MessageManager::callAsync([safe = Component::SafePointer<Editor>(this)] {
                if (safe && safe->page == 0)
                    safe->showPage(0);
            });
        };
        int initialWidth = 960;
        if (auto *display = Desktop::getInstance().getDisplays().getPrimaryDisplay())
            initialWidth =
                jlimit(660, 960,
                       int(std::min(display->userArea.getWidth() * .9,
                                    display->userArea.getHeight() * .85 * 1190.0 / 740.0)));
        setSize(initialWidth, roundToInt(initialWidth * 740.0 / 1190.0));
        showPage(AURORA_FX ? 2 : 0);
        startTimerHz(30);
    }
    ~Editor() override {
        stopTimer();
        jobs.removeAllJobs(true, -1);
        advancedEditor.reset();
        cards.clear();
        sources.clear();
        matrix.setModel(nullptr);
        browserList.setModel(nullptr);
        setLookAndFeel(nullptr);
    }
    void timerCallback() override {
        for (auto &c : cards)
            for (auto &knob : c->knobs)
                knob->refresh();
        if (page == 0 && cards.size() > 2)
            for (int i = 0; i < 3; ++i) {
                bool table = read(p, key("osc", i, "mode")) < .5f;
                for (int k = 5; k < 10; ++k)
                    cards[size_t(i)]->knobs[size_t(k)]->setEnabled(table);
                cards[size_t(i)]->knobs[4]->setEnabled(int(read(p, key("osc", i, "mode"))) == 3);
                cards[size_t(i)]->choices[1]->setEnabled(table);
            }
        if (page == 0 && cards.size() > 2)
            for (int i = 0; i < 3; ++i) {
                float dest = read(p, key("route", i, "dest")),
                      blend = read(p, key("route", i, "balance"));
                cards[size_t(i)]->actions[1]->setButtonText(dest >= .75f   ? "Route: None"
                                                            : dest >= .5f  ? "Route: Direct"
                                                            : dest >= .25f ? "Route: Main"
                                                            : blend < -50  ? "Route: Filter 1"
                                                            : blend > 50   ? "Route: Filter 2"
                                                                           : "Route: Both filters");
            }
        if (roll)
            roll->repaint();
        undo.setEnabled(p.undo.canUndo());
        redo.setEnabled(p.undo.canRedo());
        if (p.midiLearn.load() >= 0)
            status.setText("MIDI learn armed: move one hardware CC control.", dontSendNotification);
        else if (status.getText().startsWith("MIDI learn armed"))
            status.setText("MIDI mapping learned; included in the preset.", dontSendNotification);
        if (browser->ready.exchange(false)) {
            const ScopedLock lock(browser->lock);
            if (browser->message == "Preset folder scanned")
                filterPresets();
            if (browser->message.isNotEmpty()) {
                status.setText(browser->message, dontSendNotification);
                if (browser->presetName.isNotEmpty()) {
                    preset.setText(browser->presetName, dontSendNotification);
                    browser->presetName.clear();
                }
            }
            if (browser->importReport.isNotEmpty()) {
                AlertWindow::showMessageBoxAsync(AlertWindow::InfoIcon, "Serum preset import",
                                                 browser->importReport.substring(0, 5000));
                browser->importReport.clear();
                showPage(page);
            }
        }
    }
    Card &card(String title, int source = 0) {
        auto panel = std::make_unique<Card>(p, title, source);
        content.addAndMakeVisible(*panel);
        cards.push_back(std::move(panel));
        return *cards.back();
    }
    void clearPage() {
        selector.setVisible(false);
        lfoSelector.setVisible(false);
        matrix.setVisible(false);
        browserList.setVisible(false);
        search.setVisible(false);
        folder.setVisible(false);
        cards.clear();
        sources.clear();
        roll.reset();
    }
    void showPage(int next) {
        page = next;
        clearPage();
        for (auto &button : envDock)
            button->setVisible(page == 0);
        for (auto &button : lfoDock)
            button->setVisible(page == 0);
        content.activate(page);
        content.activate(page);
        for (int i = 0; i < 5; ++i)
            tabs[size_t(i)].setToggleState(page == i, dontSendNotification);
        if (page == 0) {
            for (int s = 0; s < 3; ++s) {
                auto &c = card("OSC " + String::charToString(juce_wchar('A' + s)));
                c.addCurve("osc", s);
                c.addChoice(key("osc", s, "mode"), "Engine");
                c.addKnob(key("osc", s, "position"), "Position", s + 4);
                c.addKnob(key("osc", s, "level"), "Level", s + 1);
                c.addKnob(key("osc", s, "tune"), "Tune", 7);
                c.addKnob(key("osc", s, "pan"), "Pan", 8);
                c.addKnob(key("osc", s, "grain"), "Grain ms");
                c.addKnob(key("osc", s, "unison"), "Unison");
                c.addKnob(key("osc", s, "detune"), "Detune");
                c.addKnob(key("osc", s, "warp_amount"), "Warp");
                c.addKnob(key("osc", s, "phase"), "Phase");
                c.addKnob(key("osc", s, "random"), "Random");
                c.addChoice(key("osc", s, "warp"), "Warp mode");
                c.button("Import audio / SFZ", [this, s] { chooseSample(s); });
                float destination = read(p, key("route", s, "dest")),
                      balance = read(p, key("route", s, "balance"));
                String routeLabel = destination >= .75f   ? "Route: None"
                                    : destination >= .5f  ? "Route: Direct"
                                    : destination >= .25f ? "Route: Main"
                                    : balance < -50       ? "Route: Filter 1"
                                    : balance > 50        ? "Route: Filter 2"
                                                          : "Route: Both filters";
                c.button(routeLabel, [this, s] {
                    float dest = read(p, key("route", s, "dest")),
                          balance = read(p, key("route", s, "balance"));
                    int next = dest >= .25f ? 0 : balance < -50 ? 1 : balance > 50 ? 2 : 3;
                    write(p, key("route", s, "dest"), next == 3 ? .25f : 0.f);
                    write(p, key("route", s, "balance"),
                          next == 0   ? -100.f
                          : next == 1 ? 100.f
                                      : 0.f);
                });
                c.makeCompact(1);
            }
            for (int f = 0; f < 2; ++f) {
                auto &c = card("FILTER " + String(f + 1));
                c.addChoice(key("filter", f, "type"), "Type");
                c.addKnob(key("filter", f, "cutoff"), "Cutoff Hz", 9 + f);
                c.addKnob(key("filter", f, "res"), "Resonance");
                c.addCurve("filter", f);
                c.addFilterRouting(f);
                c.makeCompact(2);
            }
            auto &env = card("ENV " + String(envIndex + 1), 4 + envIndex);
            env.addCurve("env", envIndex);
            for (auto suffix : {"a", "d", "s", "r"})
                env.addKnob(key("env", envIndex, suffix), String(suffix).toUpperCase());
            env.addKnob(key("env", envIndex, "duration"), "Duration");
            env.addKnob(key("env", envIndex, "tension"), "Bezier");
            env.button(read(p, key("env", envIndex, "mode")) < .5f ? "ADSR" : "CURVE", [this] {
                write(p, key("env", envIndex, "mode"),
                      read(p, key("env", envIndex, "mode")) < .5f ? 1 : 0);
                MessageManager::callAsync([safe = Component::SafePointer<Editor>(this)] {
                    if (safe)
                        safe->showPage(0);
                });
            });
            env.makeCompact(3);
            auto &macros = card("MACROS");
            for (int i = 0; i < 8; ++i)
                macros.addKnob(key("macro", i, "value"), "M" + String(i + 1));

            macros.makeCompact(5);
            auto &lfo = card("LFO " + String(lfoIndex + 1), 8 + lfoIndex);
            lfo.addCurve("lfo", lfoIndex);
            lfo.addChoice(key("lfo", lfoIndex, "shape"), "Shape");
            lfo.addChoice(key("lfo", lfoIndex, "division"), "Division");
            lfo.addKnob(key("lfo", lfoIndex, "rate"), "Rate");
            lfo.addKnob(key("lfo", lfoIndex, "phase"), "Phase");
            lfo.addKnob(key("lfo", lfoIndex, "delay"), "Delay");
            lfo.addKnob(key("lfo", lfoIndex, "rise"), "Rise");
            lfo.addKnob(key("lfo", lfoIndex, "tension"), "Bezier");
            lfo.button(read(p, key("lfo", lfoIndex, "sync")) > .5f ? "SYNC ON" : "SYNC OFF",
                       [this] {
                           write(p, key("lfo", lfoIndex, "sync"),
                                 read(p, key("lfo", lfoIndex, "sync")) > .5f ? 0 : 1);
                           MessageManager::callAsync([safe = Component::SafePointer<Editor>(this)] {
                               if (safe)
                                   safe->showPage(0);
                           });
                       });
            lfo.makeCompact(4);
            auto &voicing = card("VOICING");
            voicing.addChoice("voice_mode", "Mode");
            voicing.addKnob("voice_limit", "Voices");
            voicing.addKnob("portamento", "Porta ms");
            voicing.makeCompact(7);
            auto &sub = card("SUB"); sub.addKnob("sub", "Level");
            sub.addKnob(key("route",4,"balance"), "Filter"); sub.makeCompact(6);
            auto &noise = card("NOISE"); noise.addKnob("noise", "Level");
            noise.addKnob(key("route",3,"balance"), "Filter"); noise.makeCompact(6);

            lfoSelector.clear(dontSendNotification);
            for (int i = 0; i < 10; ++i)
                lfoSelector.addItem("LFO " + String(i + 1), i + 1);
            lfoSelector.setSelectedId(lfoIndex + 1, dontSendNotification);
            content.addAndMakeVisible(lfoSelector);
            selector.clear(dontSendNotification);
            for (int i = 0; i < 4; ++i)
                selector.addItem("Envelope " + String(i + 1), i + 1);
            selector.setSelectedId(envIndex + 1, dontSendNotification);
            content.addAndMakeVisible(selector);
        } else if (page == 1) {
            const char *names[]{"OSC A", "OSC B", "OSC C", "NOISE", "SUB", "FILTER 1", "FILTER 2"};
            for (int i = 0; i < 7; ++i) {
                auto &c = card(names[i]);
                if (i < 3) {
                    c.addKnob(key("osc", i, "level"), "Level");
                    c.addKnob(key("osc", i, "pan"), "Pan");
                } else if (i < 5)
                    c.addKnob(i == 3 ? "noise" : "sub", "Level");
                c.addKnob(key("route", i, "balance"), "Filter balance");
                c.addKnob(key("route", i, "bus1"), "Bus 1 send");
                c.addKnob(key("route", i, "bus2"), "Bus 2 send");
                c.addKnob(key("route", i, "dest"), "Destination");
                c.addKnob(key("route", i, "env"), "Via Env 1");
            }
        } else if (page == 3) {
            auto &lfo = card("LFO " + String(lfoIndex + 1), 8 + lfoIndex);
            lfo.addCurve("lfo", lfoIndex);
            lfo.addChoice(key("lfo", lfoIndex, "shape"), "Shape");
            lfo.addChoice(key("lfo", lfoIndex, "division"), "Division");
            lfo.addKnob(key("lfo", lfoIndex, "rate"), "Rate Hz");
            lfo.addKnob(key("lfo", lfoIndex, "phase"), "Phase");
            lfo.addKnob(key("lfo", lfoIndex, "delay"), "Delay");
            lfo.addKnob(key("lfo", lfoIndex, "rise"), "Rise");
            lfo.addKnob(key("lfo", lfoIndex, "tension"), "Bezier");
            lfo.button(read(p, key("lfo", lfoIndex, "sync")) > .5f ? "Tempo sync: ON"
                                                                   : "Tempo sync: OFF",
                       [this] {
                           write(p, key("lfo", lfoIndex, "sync"),
                                 read(p, key("lfo", lfoIndex, "sync")) > .5f ? 0 : 1);
                           showPage(3);
                       });
            for (int i = 1; i < 26; ++i) {
                auto *choice =
                    dynamic_cast<AudioParameterChoice *>(p.state.getParameter("mod0_source"));
                auto b = std::make_unique<aurora_ui::SourceButton>(choice->choices[i], i);
                content.addAndMakeVisible(*b);
                sources.push_back(std::move(b));
            }
            content.addAndMakeVisible(matrix);
            selector.clear(dontSendNotification);
            for (int i = 0; i < 10; ++i)
                selector.addItem("LFO " + String(i + 1), i + 1);
            selector.setSelectedId(lfoIndex + 1, dontSendNotification);
            content.addAndMakeVisible(selector);
            buildModControls();
        } else if (page == 2) {
            for (int s = 0; s < 4; ++s) {
                auto &c = card("SLOT " + String(s + 1));
                c.enableFXDrag(rackIndex, s);
                String id = "fx" + String(rackIndex) + "s" + String(s) + "_";
                c.addChoice(id + "type", "Effect");
                c.addKnob(id + "amount", "Amount");
                c.addKnob(id + "mix", "Mix");
                c.addKnob(id + "time", "Time / tone");
                if (s == 0)
                    c.button("Import rack IR", [this] { chooseIR(); });
            }
            auto &routing = card(AURORA_FX ? "INPUT / SENDS" : "OSC 1 / SENDS");
            routing.addKnob(key("route", 0, "balance"), "Filter blend");
            routing.addKnob(key("route", 0, "bus1"), "Bus 1 %");
            routing.addKnob(key("route", 0, "bus2"), "Bus 2 %");
            routing.addKnob(key("route", 0, "dest"), "Destination");
            routing.addKnob("master", "Master");
            selector.clear(dontSendNotification);
            selector.addItem("FX BUS 1", 1);
            selector.addItem("FX BUS 2", 2);
            selector.addItem("MAIN RACK", 3);
            selector.setSelectedId(rackIndex + 1, dontSendNotification);
            content.addAndMakeVisible(selector);
        } else if (page == 4) {
            auto &c = card("CLIP / ARPEGGIATOR");
            c.addChoice("seq_mode", "Mode");
            c.addChoice("seq_div", "Division");
            c.addKnob("seq_bpm", "Fallback BPM");
            c.addKnob("seq_gate", "Gate");
            c.addChoice("arp_order", "Direction");
            c.addKnob("arp_octaves", "Octaves");
            c.addKnob("arp_swing", "Swing");
            auto &performance = card("VOICING / PERFORMANCE");
            performance.addChoice("voice_mode", "Voicing");
            performance.addChoice("key", "Key");
            performance.addChoice("scale", "Scale");
            performance.addKnob("voice_limit", "Voices");
            performance.addKnob("portamento", "Portamento ms");
            performance.addKnob("transpose", "Transpose");
            performance.button("Always / Scaled", [this] {
                write(p, "porta_always", read(p, "porta_always") > .5f ? 0 : 1);
            });
            roll = std::make_unique<PianoRoll>(p);
            content.addAndMakeVisible(*roll);
        } else {
            content.addAndMakeVisible(browserList);
            content.addAndMakeVisible(search);
            content.addAndMakeVisible(folder);
        }
        layoutPage();
    }
    void buildModControls() {
        while (cards.size() > 1)
            cards.pop_back();
        auto &c = card("MOD SLOT " + String(modIndex + 1) + " / SOURCE - DESTINATION - AMOUNT");
        c.addChoice(key("mod", modIndex, "source"), "Source");
        c.addChoice(key("mod", modIndex, "target"), "Target");
        c.addKnob(key("mod", modIndex, "depth"), "Amount");
        c.addKnob(key("mod", modIndex, "curve"), "Curve");
        c.button("Clear route", [this] {
            write(p, key("mod", modIndex, "source"), 0);
            write(p, key("mod", modIndex, "target"), 0);
            write(p, key("mod", modIndex, "depth"), 0);
        });
        layoutPage();
    }
    void layoutPage() {
        int w = 1190, gap = 12;
        for(auto &b:filterTabs){b.setVisible(page==0);b.toFront(false);}
        if (cards.empty() && page != 5) {
            content.setSize(w, 100);
            return;
        }
        if (page == 0) {
            const char *oscPanels[]{"OscPage/OSC A","OscPage/OSC B","OscPage/OSC C"};
            for(int i=0;i<3;++i)cards[size_t(i)]->setBounds(aurora_ui::reference::bounds(oscPanels[i]));
            for(int i=0;i<2;++i){cards[size_t(3+i)]->setBounds(986,0,204,318);
                cards[size_t(3+i)]->setVisible(i==selectedFilter);
                filterTabs[size_t(i)].setToggleState(i==selectedFilter,dontSendNotification);}
            cards[5]->setBounds(166,356,350,225);
            cards[6]->setBounds(0,318,166,265);
            cards[7]->setBounds(518,356,520,225);
            cards[8]->setBounds(1038,356,152,225);
            cards[9]->setBounds(0,0,76,318);
            cards[10]->setBounds(886,0,100,318);
            for(int i=0;i<4;++i){envDock[size_t(i)]->setBounds(166+i*87,387,86,37);
                envDock[size_t(i)]->setToggleState(i==envIndex,dontSendNotification);}
            for(int i=0;i<10;++i){lfoDock[size_t(i)]->setBounds(518+i*52,387,51,37);
                lfoDock[size_t(i)]->setToggleState(i==lfoIndex,dontSendNotification);}
            content.setSize(w,583);selector.setVisible(false);lfoSelector.setVisible(false);
        } else if (page == 1) {
            int cw = (w - 3 * gap) / 4;
            for (size_t i = 0; i < cards.size(); ++i)
                cards[i]->setBounds(int(i) % 4 * (cw + gap), int(i) / 4 * 310, cw, 298);
            content.setSize(w, 620);
        } else if (page == 3) {
            cards[0]->setBounds(230, 0, w - 230, 310);
            selector.setBounds(w - 170, 6, 150, 25);
            for (size_t i = 0; i < sources.size(); ++i)
                sources[i]->setBounds(int(i) % 2 * 110, int(i) / 2 * 26, 104, 23);
            matrix.setBounds(0, 355, w - 340, 310);
            if (cards.size() > 1)
                cards[1]->setBounds(w - 328, 355, 328, 310);
            content.setSize(w, 677);
            selector.toFront(false);
        } else if (page == 2) {
            int cw = (w - 3 * gap) / 4;
            for (int i = 0; i < 4; ++i)
                cards[size_t(i)]->setBounds(i * (cw + gap), 42, cw, 320);
            selector.setBounds(0, 0, 230, 29);
            cards[4]->setBounds(0, 375, w, 170);
            content.setSize(w, 560);
        } else if (page == 4) {
            cards[0]->setBounds(0, 0, int(w * .58), 330);
            cards[1]->setBounds(int(w * .58) + 10, 0, w - int(w * .58) - 10, 330);
            if (roll)
                roll->setBounds(0, 342, w, 230);
            content.setSize(w, 584);
        } else {
            folder.setBounds(0, 0, 210, 30);
            search.setBounds(225, 0, w - 225, 30);
            browserList.setBounds(0, 44, w, 490);
            content.setSize(w, 548);
        }
        float fit =
            std::min(1.f, float(viewport.getHeight()) / float(std::max(1, content.getHeight())));
        content.setTransform(AffineTransform::scale(fit));
        viewport.setScrollBarsShown(false, false);
    }
    void resized() override {
        constexpr int w=aurora_ui::reference::width, logicalHeight=aurora_ui::reference::height;
        brand.setBounds(10,4,150,31);advanced.setBounds(9,39,148,23);
        for(int i=0;i<5;++i)tabs[size_t(i)].setBounds(166+i*68,0,67,68);
        preset.setBounds(516,5,550,27);
        presetsButton.setBounds(516,39,100,23);load.setBounds(623,39,90,23);
        save.setBounds(720,39,90,23);undo.setBounds(817,39,90,23);redo.setBounds(914,39,90,23);
        masterControl->setBounds(1087,4,52,60);scope.setBounds(1146,7,36,53);
        viewport.setBounds(0,69,w,583);
        filterTabs[0].setBounds(986,69,79,26);filterTabs[1].setBounds(1087,69,79,26);
        status.setBounds(170,641,1015,12);status.setFont(Font(9.f));
        pitchCaption.setBounds(3,655,43,17);modCaption.setBounds(47,655,43,17);
        pitchWheel.setSliderStyle(Slider::LinearVertical);modWheel.setSliderStyle(Slider::LinearVertical);
        pitchWheel.setBounds(8,676,29,57);modWheel.setBounds(51,676,29,57);
        modulationPicker.setBounds(95,656,285,25);modulationDrag.setBounds(95,688,285,25);
        keyboard.setBounds(393,657,790,77);
        if(advancedEditor)advancedEditor->setBounds(18,103,w-36,logicalHeight-185);
        layoutPage();
        float zoom=std::min(float(getWidth())/1190.f,float(getHeight())/740.f);
        for (int i = 0; i < getNumChildComponents(); ++i) {
            auto *child = getChildComponent(i);
            if (dynamic_cast<ResizableCornerComponent *>(child)) {
                child->setTransform({});
                child->setBounds(getWidth() - 16, getHeight() - 16, 16, 16);
            } else if (dynamic_cast<ResizableBorderComponent *>(child)) {
                child->setTransform({});
                child->setBounds(getLocalBounds());
            } else
                child->setTransform(AffineTransform::scale(zoom));
        }
    }
    void paint(Graphics &g) override {
        g.fillAll(aurora_ui::ink);
        g.setColour(aurora_ui::accent);
        g.fillRect(0, 0, getWidth(), 3);
        g.setColour(aurora_ui::line);
        g.drawHorizontalLine(63, 8, float(getWidth() - 8));
    }
    void chooseSample(int slot) {
        chooser = std::make_unique<FileChooser>("Import sample or SFZ", File{},
                                                "*.wav;*.aif;*.aiff;*.flac;*.sfz");
        chooser->launchAsync(
            FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [safe = Component::SafePointer<Editor>(this), slot](const FileChooser &fc) {
                if (safe && fc.getResult().existsAsFile()) {
                    auto file = fc.getResult();
                    auto result = safe->browser;
                    auto *processor = &safe->p;
                    safe->jobs.addJob([processor, result, file, slot] {
                        bool ok = file.hasFileExtension("sfz") ? processor->loadSFZ(slot, file)
                                                               : processor->loadSample(slot, file);
                        const ScopedLock lock(result->lock);
                        result->message =
                            ok ? "Imported " + file.getFileName() : "Sample import failed";
                        result->ready.store(true);
                    });
                }
            });
    }
    void chooseIR() {
        int rack = rackIndex;
        chooser = std::make_unique<FileChooser>("Import rack impulse response", File{},
                                                "*.wav;*.aif;*.flac");
        chooser->launchAsync(
            FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
            [safe = Component::SafePointer<Editor>(this), rack](const FileChooser &fc) {
                if (safe && fc.getResult().existsAsFile()) {
                    bool ok = safe->p.loadIR(rack, fc.getResult());
                    safe->status.setText(ok ? "IR imported for this rack" : "IR import failed",
                                         dontSendNotification);
                }
            });
    }
    void choosePreset(bool writing) {
        chooser = std::make_unique<FileChooser>(
            writing ? "Save Aurora preset" : "Load Aurora / Serum preset", File{},
            writing ? "*.aurora" : "*.aurora;*.serumpreset;*.SerumPreset");
        chooser->launchAsync(
            (writing ? FileBrowserComponent::saveMode : FileBrowserComponent::openMode) |
                FileBrowserComponent::canSelectFiles,
            [safe = Component::SafePointer<Editor>(this), writing](const FileChooser &fc) {
                if (!safe || fc.getResult() == File{})
                    return;
                if (writing) {
                    MemoryBlock data;
                    safe->p.getStateInformation(data);
                    auto file = fc.getResult().withFileExtension("aurora");
                    bool ok = file.replaceWithData(data.getData(), data.getSize());
                    safe->preset.setText(file.getFileNameWithoutExtension(), dontSendNotification);
                    safe->status.setText(ok ? "Preset saved" : "Could not save preset",
                                         dontSendNotification);
                } else
                    safe->loadFile(fc.getResult());
            });
    }
    void loadFile(File file) {
        status.setText(file.hasFileExtension("serumpreset") ? "Importing Serum preset..."
                                                            : "Loading preset...",
                       dontSendNotification);
        auto *processor = &p;
        auto result = browser;
        jobs.addJob([processor, result, file] {
            MemoryBlock data;
            bool foreign = file.hasFileExtension("serumpreset");
            SerumImportReport report;
            bool ok = false;
            if (foreign)
                ok = processor->importSerumPreset(file, report);
            else if (file.getSize() > 0 && file.getSize() <= 128 * 1024 * 1024 &&
                     file.loadFileAsData(data)) {
                MemoryBlock binary;
                if (decodePreset(data.getData(), int(data.getSize()), binary)) {
                    auto xml =
                        AudioProcessor::getXmlFromBinary(binary.getData(), int(binary.getSize()));
                    if (xml && xml->hasTagName("AuroraState")) {
                        processor->setStateInformation(data.getData(), int(data.getSize()));
                        ok = true;
                    }
                }
            }
            const ScopedLock lock(result->lock);
            result->message = ok ? file.getFileNameWithoutExtension()
                                 : "Preset load failed; previous sound preserved";
            if (foreign)
                result->importReport = report.text();
            if (ok)
                result->presetName = file.getFileNameWithoutExtension();
            result->ready.store(true);
        });
    }
    void chooseFolder() {
        chooser = std::make_unique<FileChooser>("Choose Aurora preset folder", File{}, "*");
        chooser->launchAsync(
            FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
            [safe = Component::SafePointer<Editor>(this)](const FileChooser &fc) {
                if (safe && fc.getResult().isDirectory()) {
                    auto directory = fc.getResult();
                    auto result = safe->browser;
                    safe->jobs.addJob([directory, result] {
                        auto files = directory.findChildFiles(
                            File::findFiles, true, "*.aurora;*.serumpreset;*.SerumPreset");
                        const ScopedLock lock(result->lock);
                        result->files = std::move(files);
                        result->message = "Preset folder scanned";
                        result->ready.store(true);
                    });
                }
            });
    }
    void filterPresets() {
        Array<File> files;
        {
            const ScopedLock lock(browser->lock);
            files = browser->files;
        }
        browserModel.files.clear();
        browserModel.names.clear();
        for (const auto &file : files)
            if (file.getFileName().containsIgnoreCase(search.getText())) {
                browserModel.files.add(file);
                browserModel.names.add(file.getFileNameWithoutExtension());
            }
        browserList.updateContent();
        browserList.repaint();
    }
};
