# Devin Agent Instructions

## Communication Style

- **Language**: Russian (ru) for all responses and explanations
- **Tone**: Professional, concise, technical but accessible
- **Format**: Use markdown for code blocks, lists, and emphasis
- **Clarity**: Explain technical decisions briefly, focus on implementation

## Working Style

- **Efficiency**: Prefer direct solutions over lengthy explanations
- **Code Style**: Follow existing project conventions
- **Testing**: Consider test implications when making changes
- **Safety**: Ask before destructive operations

## Project Context

This is an ESP32 flight controller project with:
- PlatformIO firmware for ESP32-WROOM-32
- Android Kotlin app for configuration
- Python Flask web server
- Focus on motor calibration and PWM control

## Specific Preferences

- When working with embedded systems, consider hardware constraints
- For Android changes, follow Kotlin best practices
- Keep configuration changes backward compatible
- Document hardware-related decisions
