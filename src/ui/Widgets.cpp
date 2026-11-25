#include "ui/Widgets.h"
#include "ui/Theme.h"

#include <algorithm>

namespace ui
{
// ------------------------------------------------------------------ Button
PinkButton::PinkButton(sf::Font& font, const std::string& label,
    const sf::Vector2f& position, const sf::Vector2f& size, bool primary)
    : m_font(font), m_label(label), m_position(position), m_size(size),
      m_primary(primary), m_enabled(true), m_hoverAmount(0.0f), m_pressed(false)
{
}

void PinkButton::setLabel(const std::string& label) { m_label = label; }
void PinkButton::setEnabled(bool enabled) { m_enabled = enabled; }
bool PinkButton::isEnabled() const { return m_enabled; }
void PinkButton::setPosition(const sf::Vector2f& position) { m_position = position; }
const sf::Vector2f& PinkButton::getPosition() const { return m_position; }

void PinkButton::update(const sf::Vector2f& mousePosition, float deltaSeconds)
{
    const bool hovered = m_enabled && sf::FloatRect(m_position, m_size).contains(mousePosition);
    m_pressed = hovered && sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
    const float target = hovered ? 1.0f : 0.0f;
    m_hoverAmount += (target - m_hoverAmount) * std::min(1.0f, deltaSeconds * 12.0f);
}

bool PinkButton::handleClick(const sf::Vector2f& mousePosition) const
{
    return m_enabled && sf::FloatRect(m_position, m_size).contains(mousePosition);
}

void PinkButton::draw(sf::RenderTarget& target) const
{
    sf::Color fill = theme::Disabled;
    if (m_enabled)
    {
        if (m_primary)
            fill = m_pressed ? theme::PrimaryPressed
                 : (m_hoverAmount > 0.5f ? theme::PrimaryHover : theme::Primary);
        else
            fill = m_hoverAmount > 0.5f ? theme::LightPanel : theme::White;
    }
    // Gentle grow-on-hover so buttons feel alive under the mouse.
    const float grow = 1.0f + (m_enabled ? m_hoverAmount * 0.035f : 0.0f);
    const sf::Vector2f size{m_size.x * grow, m_size.y * grow};
    sf::Vector2f pos{m_position.x - (size.x - m_size.x) / 2.0f,
        m_position.y - (size.y - m_size.y) / 2.0f};
    if (m_pressed) pos.y += 1.0f;
    theme::drawRoundedBox(target, pos, size, 11.0f, fill,
        m_primary ? sf::Color::Transparent : theme::Accent, m_primary ? 0.0f : 1.2f);
    const sf::Vector2f drawSize = size;

    sf::Text text(m_font, m_label, theme::scaledSize(14));
    theme::applyTextScale(text);
    text.setFillColor(!m_enabled ? theme::Muted : (m_primary ? theme::White : theme::PrimaryPressed));
    const sf::FloatRect b = theme::scaledBounds(text);
    text.setPosition({pos.x + (drawSize.x - b.size.x) / 2.0f - b.position.x,
        pos.y + (drawSize.y - b.size.y) / 2.0f - b.position.y});
    target.draw(text);
}

// ---------------------------------------------------------------- Text box
PinkTextBox::PinkTextBox(sf::Font& font, const sf::Vector2f& position,
    const sf::Vector2f& size, const std::string& placeholder, bool numericOnly)
    : m_font(font), m_position(position), m_size(size), m_placeholder(placeholder),
      m_numericOnly(numericOnly), m_focused(false), m_cursorTime(0.0f), m_maxLength(300)
{
}

void PinkTextBox::setText(const std::string& text)
{
    if (m_phoneFormat)
    {
        m_text = text;
        applyPhoneFormat();
        return;
    }
    m_text = text.size() > m_maxLength ? text.substr(0, m_maxLength) : text;
}

void PinkTextBox::setPhoneFormat(bool on)
{
    m_phoneFormat = on;
    if (on) applyPhoneFormat();
}

void PinkTextBox::applyPhoneFormat()
{
    // Keep only the digits (at most 11) and show them as 0300-1234567:
    // the dash appears on its own once the fifth digit is typed.
    std::string digits;
    for (const char c : m_text)
        if (c >= '0' && c <= '9') digits += c;
    if (digits.size() > 11) digits = digits.substr(0, 11);
    m_text = digits.size() > 4 ? digits.substr(0, 4) + "-" + digits.substr(4) : digits;
}
const std::string& PinkTextBox::getText() const { return m_text; }
void PinkTextBox::setFocused(bool focused) { m_focused = focused; }
bool PinkTextBox::isFocused() const { return m_focused; }
void PinkTextBox::setMaxLength(std::size_t maxLength) { m_maxLength = maxLength; }
void PinkTextBox::setAllowDecimal(bool allow) { m_decimalAllowed = allow; }
void PinkTextBox::setPosition(const sf::Vector2f& position) { m_position = position; }
void PinkTextBox::setSize(const sf::Vector2f& size) { m_size = size; }

bool PinkTextBox::handleClick(const sf::Vector2f& mousePosition)
{
    m_focused = sf::FloatRect(m_position, m_size).contains(mousePosition);
    return m_focused;
}

void PinkTextBox::handleTextEntered(char32_t unicode)
{
    if (!m_focused) return;
    if (m_phoneFormat)
    {
        if (unicode == 8)  // Backspace removes the last DIGIT
        {
            std::string digits;
            for (const char c : m_text)
                if (c >= '0' && c <= '9') digits += c;
            if (!digits.empty()) digits.pop_back();
            m_text = digits;
            applyPhoneFormat();
            return;
        }
        if (unicode >= '0' && unicode <= '9')
        {
            std::string digits;
            for (const char c : m_text)
                if (c >= '0' && c <= '9') digits += c;
            if (digits.size() < 11) digits += static_cast<char>(unicode);
            m_text = digits;
            applyPhoneFormat();
        }
        return;
    }
    if (unicode == 8)  // Backspace
    {
        if (!m_text.empty()) m_text.pop_back();
        return;
    }
    if (unicode < 32 || unicode > 126 || m_text.size() >= m_maxLength) return;
    const char c = static_cast<char>(unicode);
    if (!m_numericOnly || (c >= '0' && c <= '9')) m_text += c;
    else if (m_decimalAllowed && c == '.'
        && m_text.find('.') == std::string::npos) m_text += c;
}

void PinkTextBox::update(float deltaSeconds)
{
    m_cursorTime += deltaSeconds;
    if (m_cursorTime > 1.0f) m_cursorTime -= 1.0f;
}

void PinkTextBox::draw(sf::RenderTarget& target) const
{
    theme::drawRoundedBox(target, m_position, m_size, 10.0f, theme::Input,
        m_focused ? theme::Focus : theme::Border, m_focused ? 2.0f : 1.2f);

    // Show the tail of long text (e.g. file paths) so the end stays visible.
    std::string shown = m_text.empty() ? m_placeholder : m_text;
    sf::Text text(m_font, shown, theme::scaledSize(15));
    theme::applyTextScale(text);
    while (!shown.empty() && theme::scaledBounds(text).size.x > m_size.x - 26.0f)
    {
        shown.erase(shown.begin());
        text.setString(shown);
    }
    text.setFillColor(m_text.empty() ? theme::Muted : theme::Text);
    const sf::FloatRect b = theme::scaledBounds(text);
    text.setPosition({m_position.x + 12.0f,
        m_position.y + (m_size.y - b.size.y) / 2.0f - b.position.y});
    target.draw(text);

    if (m_focused && m_cursorTime < 0.55f)
    {
        sf::RectangleShape cursor({1.6f, m_size.y - 18.0f});
        cursor.setPosition({m_text.empty() ? m_position.x + 13.0f
                : m_position.x + 13.0f + theme::scaledBounds(text).size.x + 2.0f,
            m_position.y + 9.0f});
        cursor.setFillColor(theme::PrimaryPressed);
        target.draw(cursor);
    }
}


// -------------------------------------------------------------- Dropdown
PinkDropdown::PinkDropdown(sf::Font& font, const sf::Vector2f& position,
    const sf::Vector2f& size)
    : m_font(font), m_position(position), m_size(size), m_selected(0),
      m_open(false), m_hovered(-1)
{
}

void PinkDropdown::setOptions(const std::vector<std::string>& options)
{
    m_options = options;
    if (m_selected >= static_cast<int>(m_options.size())) m_selected = 0;
    m_open = false;
}

void PinkDropdown::setSelected(int index)
{
    if (index >= 0 && index < static_cast<int>(m_options.size())) m_selected = index;
}
int PinkDropdown::selected() const { return m_selected; }
std::string PinkDropdown::selectedText() const
{
    return m_options.empty() ? std::string() : m_options[static_cast<std::size_t>(m_selected)];
}
void PinkDropdown::setPosition(const sf::Vector2f& position) { m_position = position; }

void PinkDropdown::setSize(const sf::Vector2f& size)
{
    m_size = size;
}
bool PinkDropdown::isOpen() const { return m_open; }
void PinkDropdown::setOpen(bool open) { m_open = open; }
void PinkDropdown::close() { m_open = false; }

float PinkDropdown::listDirection() const
{
    const float listH = m_size.y * static_cast<float>(m_options.size());
    return (m_position.y + m_size.y + listH <= 676.0f) ? 1.0f : -1.0f;
}

sf::FloatRect PinkDropdown::listBounds() const
{
    const float listH = m_size.y * static_cast<float>(m_options.size());
    const float y = listDirection() > 0.0f ? m_position.y + m_size.y + 2.0f
                                          : m_position.y - listH - 2.0f;
    return sf::FloatRect({m_position.x, y}, {m_size.x, listH});
}

bool PinkDropdown::handleClick(const sf::Vector2f& mousePosition)
{
    if (m_open)
    {
        const sf::FloatRect list = listBounds();
        if (list.contains(mousePosition))
        {
            const int idx = static_cast<int>((mousePosition.y - list.position.y) / m_size.y);
            if (idx >= 0 && idx < static_cast<int>(m_options.size())) m_selected = idx;
            m_open = false;
            return true;
        }
        if (sf::FloatRect(m_position, m_size).contains(mousePosition))
        {
            m_open = false;
            return true;
        }
        m_open = false;  // clicked elsewhere: just close, swallow the click
        return true;
    }
    if (sf::FloatRect(m_position, m_size).contains(mousePosition))
    {
        m_open = true;
        return true;
    }
    return false;
}

void PinkDropdown::update(const sf::Vector2f& mousePosition, float /*deltaSeconds*/)
{
    m_hovered = -1;
    if (!m_open) return;
    const sf::FloatRect list = listBounds();
    if (list.contains(mousePosition))
        m_hovered = static_cast<int>((mousePosition.y - list.position.y) / m_size.y);
}

void PinkDropdown::draw(sf::RenderTarget& target) const
{
    theme::drawRoundedBox(target, m_position, m_size, 9.0f, theme::Input,
        m_open ? theme::Focus : theme::Border, m_open ? 2.0f : 1.2f);
    sf::Text text(m_font, selectedText(), theme::scaledSize(13));
    theme::applyTextScale(text);
    text.setFillColor(theme::Text);
    const sf::FloatRect b = theme::scaledBounds(text);
    text.setPosition({m_position.x + 10.0f,
        m_position.y + (m_size.y - b.size.y) / 2.0f - b.position.y});
    target.draw(text);
    // Chevron: a small downward triangle at the right edge.
    sf::ConvexShape chevron;
    chevron.setPointCount(3);
    const sf::Vector2f c{m_position.x + m_size.x - 16.0f, m_position.y + m_size.y / 2.0f};
    chevron.setPoint(0, {c.x - 5.0f, c.y - 3.0f});
    chevron.setPoint(1, {c.x + 5.0f, c.y - 3.0f});
    chevron.setPoint(2, {c.x, c.y + 4.0f});
    chevron.setFillColor(theme::PrimaryPressed);
    target.draw(chevron);
}

void PinkDropdown::drawList(sf::RenderTarget& target) const
{
    if (!m_open || m_options.empty()) return;
    const sf::FloatRect list = listBounds();
    theme::drawRoundedBox(target, list.position, list.size, 9.0f, theme::White,
        theme::Border, 1.2f);
    for (std::size_t i = 0; i < m_options.size(); ++i)
    {
        const float y = list.position.y + static_cast<float>(i) * m_size.y;
        if (static_cast<int>(i) == m_hovered || static_cast<int>(i) == m_selected)
            theme::drawRoundedBox(target, {list.position.x + 3.0f, y + 2.0f},
                {list.size.x - 6.0f, m_size.y - 4.0f}, 7.0f, theme::LightPanel);
        sf::Text text(m_font, m_options[i], theme::scaledSize(13));
        theme::applyTextScale(text);
        text.setFillColor(static_cast<int>(i) == m_selected ? theme::ActiveText : theme::Text);
        const sf::FloatRect b = theme::scaledBounds(text);
        text.setPosition({list.position.x + 10.0f,
            y + (m_size.y - b.size.y) / 2.0f - b.position.y});
        target.draw(text);
    }
}

}  // namespace ui
