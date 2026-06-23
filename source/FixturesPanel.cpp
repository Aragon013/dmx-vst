#include "FixturesPanel.h"
#include "FixtureModel.h"

FixturesPanel::FixturesPanel (DmxVstAudioProcessor& p)
    : processorRef (p)
{
    addAndMakeVisible (list);
    list.setRowHeight (44);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff0f1115));

    addAndMakeVisible (addButton);
    addButton.onClick = [this] { showEditorForNew(); };

    addAndMakeVisible (editButton);
    editButton.onClick = [this] { showEditorForEdit (list.getSelectedRow()); };

    addAndMakeVisible (removeButton);
    removeButton.onClick = [this] { removeSelected(); };
}

void FixturesPanel::refresh()
{
    list.updateContent();
    list.repaint();
}

int FixturesPanel::getNumRows()
{
    return (int) processorRef.getFixtures().size();
}

void FixturesPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    const auto& fixtures = processorRef.getFixtures();
    if (! juce::isPositiveAndBelow (row, (int) fixtures.size()))
        return;

    const auto& f = fixtures[(size_t) row];

    if (selected)
        g.fillAll (juce::Colour (0xff2a3140));

    g.setColour (juce::Colour (0xffffb020));
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));

    auto title = f.name;
    if (f.manufacturer.isNotEmpty() || f.model.isNotEmpty())
        title << "  (" << juce::String (f.manufacturer + " " + f.model).trim() << ")";
    g.drawText (title, 10, 4, width - 20, 20, juce::Justification::centredLeft, true);

    // Lista de tipos de canal
    juce::StringArray chans;
    for (const auto& c : f.channels)
        chans.add (channelTypeToString (c.type));

    g.setColour (juce::Colours::lightgrey);
    g.setFont (juce::FontOptions (12.0f));
    const auto info = "U" + juce::String (f.universe)
                    + "  DMX " + juce::String (f.startAddress)
                    + "-" + juce::String (f.lastAddress())
                    + "  [" + chans.joinIntoString (", ") + "]";
    g.drawText (info, 10, 22, width - 20, 18, juce::Justification::centredLeft, true);

    g.setColour (juce::Colour (0xff262a31));
    g.drawHorizontalLine (height - 1, 0.0f, (float) width);
}

void FixturesPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    showEditorForEdit (row);
}

void FixturesPanel::showEditorForNew()
{
    editingIndex = -1;
    editor = std::make_unique<FixtureEditor>();

    editor->onCommit = [this] (std::vector<Fixture> fixtures)
    {
        for (const auto& f : fixtures)
            processorRef.addFixture (f);
        refresh();
        closeEditor();
    };
    editor->onCancel = [this] { closeEditor(); };

    addAndMakeVisible (*editor);
    editor->setBounds (getLocalBounds());
}

void FixturesPanel::showEditorForEdit (int index)
{
    const auto& fixtures = processorRef.getFixtures();
    if (! juce::isPositiveAndBelow (index, (int) fixtures.size()))
        return;

    editingIndex = index;
    editor = std::make_unique<FixtureEditor>();
    editor->loadFixture (fixtures[(size_t) index]);

    editor->onCommit = [this] (std::vector<Fixture> fixtures)
    {
        if (! fixtures.empty() && editingIndex >= 0)
            processorRef.updateFixture (editingIndex, fixtures.front());
        refresh();
        closeEditor();
    };
    editor->onCancel = [this] { closeEditor(); };

    addAndMakeVisible (*editor);
    editor->setBounds (getLocalBounds());
}

void FixturesPanel::closeEditor()
{
    editor.reset();
    editingIndex = -1;
}

void FixturesPanel::removeSelected()
{
    const int row = list.getSelectedRow();
    if (row >= 0)
    {
        processorRef.removeFixture (row);
        refresh();
    }
}

void FixturesPanel::resized()
{
    auto area = getLocalBounds().reduced (10);

    auto buttonRow = area.removeFromTop (36);
    addButton.setBounds (buttonRow.removeFromLeft (140));
    buttonRow.removeFromLeft (8);
    editButton.setBounds (buttonRow.removeFromLeft (100));
    buttonRow.removeFromLeft (8);
    removeButton.setBounds (buttonRow.removeFromLeft (100));

    area.removeFromTop (8);
    list.setBounds (area);

    if (editor != nullptr)
        editor->setBounds (getLocalBounds());
}
