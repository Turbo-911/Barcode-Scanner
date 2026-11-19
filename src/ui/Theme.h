#pragma once

// Theme.h — every colour, size and drawing helper for the pink GUI lives here.
// Palette (Phase 1 prompt): soft pink / girlish, but dark-plum text so it
// stays readable, and barcodes are ALWAYS pure black on pure white.
#include <SFML/Graphics.hpp>
#include <algorithm>
#include <cmath>
#include <string>

namespace ui
{
namespace theme
{
    inline const sf::Color Canvas(0xFFF5F8FF);        // window background
    inline const sf::Color SidebarBg(0xFFD6E7FF);     // sidebar / header pink
    inline const sf::Color LightPanel(0xFFE5EEFF);    // light pink panel
    inline const sf::Color White(0xFFFFFFFF);
    inline const sf::Color Border(0xFFC2D4FF);
    inline const sf::Color Primary(0xFF5C8AFF);       // primary button
    inline const sf::Color PrimaryHover(0xFF7AA2FF);
    inline const sf::Color PrimaryPressed(0xE94F7AFF);
    inline const sf::Color Accent(0xFF8FABFF);
    inline const sf::Color Lavender(0xC77DFFFF);      // used sparingly
    inline const sf::Color Active(0xFFE5EEFF);        // active nav pill
    inline const sf::Color ActiveText(0xC2185BFF);
    inline const sf::Color Input(0xFFFFFBFF);         // textbox background
    inline const sf::Color Focus(0xFF5C8AFF);
    inline const sf::Color Heading(0x571C33FF);       // dark plum headings
    inline const sf::Color Text(0x6D213CFF);          // body text
    inline const sf::Color Muted(0xA86B7EFF);
    inline const sf::Color Success(0x2EB67DFF);
    inline const sf::Color SuccessBackground(0xE6F7EFFF);
    inline const sf::Color Error(0xD64550FF);
    inline const sf::Color ErrorBackground(0xFFE9ECFF);
    inline const sf::Color Disabled(0xD9C6CEFF);

    inline const float SidebarWidth = 220.0f;

    // Display scale (layout units -> window pixels), set by applyView().
    // Text is RASTERISED at its on-screen size and then scaled back to
    // layout units, so glyphs stay crisp instead of being magnified
    // from a small bitmap (the cause of blurry text in big windows).
    inline float uiScale = 1.0f;
    // kTextBoost: every font size in the app passes through here,
    // and the user found the text a little small - so all text is
    // scaled up by this one factor (layout stays identical).
    inline constexpr float kTextBoost = 1.15f;
    inline unsigned int scaledSize(unsigned int base)
    {
        return static_cast<unsigned int>(base * uiScale * kTextBoost + 0.5f);
    }
    inline void applyTextScale(sf::Text& text)
    {
        if (uiScale != 1.0f) text.setScale({1.0f / uiScale, 1.0f / uiScale});
    }
    // A text's bounds in LAYOUT units (getLocalBounds is in raster units).
    inline sf::FloatRect scaledBounds(const sf::Text& text)
    {
        const sf::FloatRect b = text.getLocalBounds();
        const float inv = 1.0f / uiScale;
        return sf::FloatRect({b.position.x * inv, b.position.y * inv},
            {b.size.x * inv, b.size.y * inv});
    }
    inline const float Radius = 14.0f;

    // Animated vertical gradient background (top colour breathes with time).
    inline void drawGradientBackground(sf::RenderTarget& target, const sf::Vector2f& size, float pulse)
    {
        const std::uint8_t lift = static_cast<std::uint8_t>(6.0f + 6.0f * pulse);  // 0..12
        const sf::Color top(0xFF, static_cast<std::uint8_t>(0xE3 + lift), static_cast<std::uint8_t>(0xEE), 0xFF);
        const sf::Color bottom = Canvas;
        sf::VertexArray quad(sf::PrimitiveType::TriangleStrip, 4);
        quad[0].position = {0.0f, 0.0f};            quad[0].color = top;
        quad[1].position = {size.x, 0.0f};          quad[1].color = top;
        quad[2].position = {0.0f, size.y};          quad[2].color = bottom;
        quad[3].position = {size.x, size.y};        quad[3].color = bottom;
        target.draw(quad);
    }

    // Filled rounded rectangle with an optional outline, built from
    // rectangles + corner circles (SFML has no rounded-rect shape).
    inline void drawRoundedBox(sf::RenderTarget& target, const sf::Vector2f& pos,
        const sf::Vector2f& size, float radius, const sf::Color& fill,
        const sf::Color& outline = sf::Color::Transparent, float outlineThickness = 0.0f)
    {
        if (outlineThickness > 0.0f)
        {
            drawRoundedBox(target,
                {pos.x - outlineThickness, pos.y - outlineThickness},
                {size.x + 2.0f * outlineThickness, size.y + 2.0f * outlineThickness},
                radius + outlineThickness, outline);
        }
        const float r = std::min(radius, std::min(size.x, size.y) / 2.0f);
        sf::RectangleShape midH({size.x - 2.0f * r, size.y});
        midH.setPosition({pos.x + r, pos.y});
        midH.setFillColor(fill);
        target.draw(midH);
        sf::RectangleShape midV({size.x, size.y - 2.0f * r});
        midV.setPosition({pos.x, pos.y + r});
        midV.setFillColor(fill);
        target.draw(midV);
        const sf::Vector2f corners[4] = {
            {pos.x + r, pos.y + r},
            {pos.x + size.x - r, pos.y + r},
            {pos.x + r, pos.y + size.y - r},
            {pos.x + size.x - r, pos.y + size.y - r}};
        for (const auto& c : corners)
        {
            sf::CircleShape circle(r);
            circle.setPosition({c.x - r, c.y - r});
            circle.setFillColor(fill);
            target.draw(circle);
        }
    }

    inline void drawCard(sf::RenderTarget& target, const sf::FloatRect& bounds)
    {
        drawRoundedBox(target, {bounds.position.x + 2.0f, bounds.position.y + 4.0f},
            bounds.size, Radius, sf::Color(0xEBC9D6B0));  // soft shadow
        drawRoundedBox(target, bounds.position, bounds.size, Radius, White, Border, 1.2f);
    }

    // Small pill-shaped status label, e.g. "PASS", "Decode Success".
    inline void drawStatus(sf::RenderTarget& target, sf::Font& font,
        const sf::Vector2f& position, const sf::Vector2f& size,
        const std::string& label, bool success)
    {
        drawRoundedBox(target, position, size, size.y / 2.0f,
            success ? SuccessBackground : ErrorBackground);
        sf::Text text(font, label, scaledSize(13));
        applyTextScale(text);
        text.setFillColor(success ? Success : Error);
        const sf::FloatRect b = scaledBounds(text);
        text.setPosition({position.x + (size.x - b.size.x) / 2.0f - b.position.x,
            position.y + (size.y - b.size.y) / 2.0f - b.position.y});
        target.draw(text);
    }

    // Heart drawn from the classic parametric heart curve and filled
    // as a triangle fan - a true heart silhouette at any size.
    inline void drawHeart(sf::RenderTarget& target, const sf::Vector2f& centre, float s, sf::Color color)
    {
        const int steps = 42;
        sf::VertexArray fan(sf::PrimitiveType::TriangleFan, static_cast<std::size_t>(steps) + 2);
        fan[0].position = centre;
        fan[0].color = color;
        const float k = s / 17.0f;
        for (int i = 0; i <= steps; ++i)
        {
            const float a = static_cast<float>(i) / static_cast<float>(steps) * 6.2831853f;
            const float sn = std::sin(a);
            const float x = 16.0f * sn * sn * sn;
            const float y = 13.0f * std::cos(a) - 5.0f * std::cos(2.0f * a)
                - 2.0f * std::cos(3.0f * a) - std::cos(4.0f * a);
            fan[static_cast<std::size_t>(i) + 1].position = {centre.x + x * k, centre.y - y * k};
            fan[static_cast<std::size_t>(i) + 1].color = color;
        }
        target.draw(fan);
    }

    // Four-point sparkle (two thin triangles). Decorative only.
    inline void drawSparkle(sf::RenderTarget& target, const sf::Vector2f& centre, float s, sf::Color color)
    {
        sf::ConvexShape sparkle;
        sparkle.setPointCount(4);
        sparkle.setPoint(0, {centre.x, centre.y - s});
        sparkle.setPoint(1, {centre.x + s * 0.28f, centre.y});
        sparkle.setPoint(2, {centre.x, centre.y + s});
        sparkle.setPoint(3, {centre.x - s * 0.28f, centre.y});
        sparkle.setFillColor(color);
        target.draw(sparkle);
    }
}
}
