#pragma once

#include <SFML/Graphics.hpp>
#include <string>
#include <vector>

namespace ui
{
// Rounded pink button with hover / pressed / disabled states.
class PinkButton
{
public:
    PinkButton(sf::Font& font, const std::string& label, const sf::Vector2f& position,
        const sf::Vector2f& size, bool primary = true);
    void setLabel(const std::string& label);
    void setEnabled(bool enabled);
    bool isEnabled() const;
    void update(const sf::Vector2f& mousePosition, float deltaSeconds);
    bool handleClick(const sf::Vector2f& mousePosition) const;
    void draw(sf::RenderTarget& target) const;
    void setPosition(const sf::Vector2f& position);
    const sf::Vector2f& getPosition() const;

private:
    sf::Font& m_font;
    std::string m_label;
    sf::Vector2f m_position;
    sf::Vector2f m_size;
    bool m_primary;
    bool m_enabled;
    float m_hoverAmount;
    bool m_pressed;
};

// Rounded text input with focus ring, blinking cursor and optional
// digits-only mode / maximum length.
class PinkTextBox
{
public:
    PinkTextBox(sf::Font& font, const sf::Vector2f& position, const sf::Vector2f& size,
        const std::string& placeholder, bool numericOnly = false);
    void setText(const std::string& text);
    const std::string& getText() const;
    void setFocused(bool focused);
    bool isFocused() const;
    void setMaxLength(std::size_t maxLength);
    void setAllowDecimal(bool allow);
    void setPhoneFormat(bool on);
    void setPosition(const sf::Vector2f& position);
    void setSize(const sf::Vector2f& size);
    bool handleClick(const sf::Vector2f& mousePosition);
    void handleTextEntered(char32_t unicode);
    void update(float deltaSeconds);
    void draw(sf::RenderTarget& target) const;

private:
    sf::Font& m_font;
    sf::Vector2f m_position;
    sf::Vector2f m_size;
    std::string m_placeholder;
    std::string m_text;
    bool m_numericOnly;
    bool m_decimalAllowed = false;
    bool m_phoneFormat = false;
    void applyPhoneFormat();
    bool m_focused;
    float m_cursorTime;
    std::size_t m_maxLength;
};

// Drop-down selector: a closed box showing the current value; clicking
// it opens the option list (drawn separately, on top of everything).
// Clicking an option selects it; clicking elsewhere closes the list.
class PinkDropdown
{
public:
    PinkDropdown(sf::Font& font, const sf::Vector2f& position, const sf::Vector2f& size);
    void setOptions(const std::vector<std::string>& options);
    void setSelected(int index);
    int selected() const;
    std::string selectedText() const;
    bool isOpen() const;
    void setOpen(bool open);
    void setPosition(const sf::Vector2f& position);
    void setSize(const sf::Vector2f& size);
    void close();
    // True when the click belonged to this dropdown (toggle, option
    // pick, or closing it by clicking elsewhere).
    bool handleClick(const sf::Vector2f& mousePosition);
    void update(const sf::Vector2f& mousePosition, float deltaSeconds);
    void draw(sf::RenderTarget& target) const;
    void drawList(sf::RenderTarget& target) const;

private:
    sf::FloatRect listBounds() const;
    float listDirection() const;  // +1 opens downward, -1 upward

    sf::Font& m_font;
    sf::Vector2f m_position;
    sf::Vector2f m_size;
    std::vector<std::string> m_options;
    int m_selected;
    bool m_open;
    int m_hovered;  // option index under the mouse, -1 = none
};

// Small rounded toast that appears bottom-right for a few seconds.
class ToastNotification
{
public:
    ToastNotification(sf::Font& font);
    void show(const std::string& message, bool success);
    void update(float deltaSeconds);
    void draw(sf::RenderTarget& target, const sf::Vector2f& windowSize) const;
    bool isVisible() const;

private:
    sf::Font& m_font;
    std::string m_message;
    bool m_success;
    float m_remaining;
};
}
