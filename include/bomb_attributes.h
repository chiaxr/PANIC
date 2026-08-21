#pragma once

// Bomb-wide attributes that puzzle templates read to derive their variables.
// This is the shared state that makes two bombs play differently: a puzzle's
// correct answer depends on the serial number, battery count, indicators, etc.

#include <random>
#include <string>
#include <vector>

enum class BombColor { BOMB_BLACK, BOMB_WHITE, BOMB_GREEN, BOMB_RED, BOMB_BLUE };

struct Indicator {
    std::string label;  // e.g. "CAR", "FRK", "SND"
    bool lit = false;
};

enum class PortType { SERIAL, PARALLEL, DVI, RJ45, PS2, RCA };

struct BombAttributes {
    BombColor color = BombColor::BOMB_BLACK;
    std::string serial = "AB1C2";
    int battery_count = 0;
    std::vector<Indicator> indicators;
    std::vector<PortType> ports;

    // ---- Derived queries used by puzzle logic and the defuser manual ----

    // Last numeric digit of the serial number; -1 if the serial has no digit.
    int serial_last_digit() const {
        for (auto it = serial.rbegin(); it != serial.rend(); ++it) {
            if (*it >= '0' && *it <= '9') return *it - '0';
        }
        return -1;
    }

    bool serial_last_digit_odd() const {
        const int d = serial_last_digit();
        return d >= 0 && (d % 2 == 1);
    }

    bool serial_has_vowel() const {
        for (char c : serial) {
            switch (c) {
                case 'A': case 'E': case 'I': case 'O': case 'U':
                case 'a': case 'e': case 'i': case 'o': case 'u':
                    return true;
                default:
                    break;
            }
        }
        return false;
    }

    bool has_lit_indicator(const std::string& label) const {
        for (const auto& ind : indicators) {
            if (ind.lit && ind.label == label) return true;
        }
        return false;
    }

    int lit_indicator_count() const {
        int n = 0;
        for (const auto& ind : indicators) {
            if (ind.lit) ++n;
        }
        return n;
    }

    // Number of characters in a serial number.
    static constexpr int serial_length = 6;

    // A fresh serial: 6 characters, letters + digits, always ending in a digit
    // so the "last digit" rules the modules lean on are always defined.
    static std::string random_serial(std::mt19937& rng) {
        static const char letters[] = "ABCDEFGHIJKLMNPQRSTUVWXYZ";
        static const char digits[] = "0123456789";
        std::uniform_int_distribution<int> letter(0, sizeof(letters) - 2);
        std::uniform_int_distribution<int> digit(0, sizeof(digits) - 2);
        std::uniform_int_distribution<int> coin(0, 1);

        std::string serial;
        for (int i = 0; i < serial_length - 1; ++i) {
            serial += coin(rng) ? letters[letter(rng)] : digits[digit(rng)];
        }
        serial += digits[digit(rng)];
        return serial;
    }

    // Everything else about the bomb, derived from the serial's seeded engine.
    // The serial is an input rather than a product: it is what the players type
    // in to replay a bomb.
    static BombAttributes random(std::mt19937& rng, const std::string& serial) {
        BombAttributes attrs;
        attrs.serial = serial;

        attrs.color = static_cast<BombColor>(
                std::uniform_int_distribution<int>(0, 4)(rng));

        std::uniform_int_distribution<int> coin(0, 1);

        attrs.battery_count = std::uniform_int_distribution<int>(0, 4)(rng);

        // A small pool of indicators, each independently present and lit/unlit.
        static const char* indicator_labels[] = {"CAR", "FRK", "SND", "CLR", "BOB"};
        for (const char* label : indicator_labels) {
            if (coin(rng)) attrs.indicators.push_back({label, coin(rng) != 0});
        }

        // Ports.
        static const PortType port_pool[] = {
                PortType::SERIAL, PortType::PARALLEL, PortType::DVI,
                PortType::RJ45, PortType::PS2, PortType::RCA};
        for (PortType p : port_pool) {
            if (coin(rng)) attrs.ports.push_back(p);
        }

        return attrs;
    }
};
