#include "core/model.hpp"
#include "core/extensions.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    require(static_cast<bool>(stream), "Cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
double luminance(std::uint32_t rgb) {
    auto channel = [](unsigned value) {
        const auto normalized = value / 255.0;
        return normalized <= 0.04045 ? normalized / 12.92 : std::pow((normalized + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel((rgb >> 16) & 255) + 0.7152 * channel((rgb >> 8) & 255) + 0.0722 * channel(rgb & 255);
}
double contrast(std::uint32_t a, std::uint32_t b) {
    const auto first = luminance(a), second = luminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}
bool intersects(aether::Rect a, aether::Rect b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Supply the Sleek pack directory.");
        std::set<std::string> packageIds, themeNames;
        for (const auto* name : {"Graphite", "Linen", "Aurora", "Ember"}) {
            const auto folder = std::filesystem::path(argv[1]) / name;
            std::string error;
            const auto theme = aether::parseTheme(read(folder / "theme.aethertheme"), &error);
            require(theme.has_value(), std::string(name) + " theme: " + error);
            require(themeNames.insert(theme->name).second, "Duplicate theme name");
            require(aether::parseTheme(aether::serializeTheme(*theme)) == theme, "Theme round trip changed tokens");
            double minimumText = 100.0;
            for (auto background : {theme->background, theme->surface, theme->raised}) {
                for (auto text : {theme->foreground, theme->muted}) {
                    minimumText = std::min(minimumText, contrast(text, background));
                    require(contrast(text, background) >= 4.5, std::string(name) + " text palette contrast below 4.5");
                }
                require(contrast(theme->accent, background) >= 3.0, std::string(name) + " accent contrast below 3");
            }
            const auto package = aether::parseExtension(read(folder / "widgets.aetherplugin"), &error);
            require(package.has_value(), std::string(name) + " widgets: " + error);
            require(packageIds.insert(package->id).second, "Duplicate package ID");
            require(aether::parseExtension(aether::serializeExtension(*package)) == package, "Widget round trip changed tokens");
            require(package->widgets.size() == 5, "Expected clock, focused app and three shortcuts");
            for (int dpi : {96, 144, 192}) {
                for (int direction : {-1, 1}) {
                    const int width = 1280 * dpi / 96, height = 720 * dpi / 96;
                    const int left = direction * width;
                    const aether::Rect work{left, 0, left + width, height};
                    std::vector<aether::Rect> bounds;
                    for (const auto& widget : package->widgets) {
                        require(!widget.accent || *widget.accent == theme->accent, "Widget accent differs from coordinated theme");
                        require(widget.placement.monitor.empty(), "Pack binds to an author's physical monitor");
                        require(widget.action != aether::WidgetAction::Pin, "Starter pack depends on a private application pin");
                        const auto box = aether::extensionBounds(widget, work, dpi);
                        require(box.left >= work.left && box.top >= work.top && box.right <= work.right && box.bottom <= work.bottom, "Widget outside display work area");
                        require(box.width() == widget.width * dpi / 96 && box.height() == widget.height * dpi / 96, "Widget unexpectedly clamped at supported test size");
                        for (auto other : bounds) require(!intersects(box, other), "Widgets overlap in pack's initial layout");
                        bounds.push_back(box);
                    }
                }
            }
            std::cout << name << ": theme and 5 widgets parsed and round-tripped; text contrast min=" << minimumText
                      << "; nonoverlapping bounds at 96/144/192 DPI on both coordinate sides\n";
        }
        std::cout << "PASS: 4 coordinated themes, 4 native packages, 20 widget definitions. Palette math and model geometry only; no live UI or performance acceptance.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
