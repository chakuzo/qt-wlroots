# Contributing to wlroots-qt-compositor

Thank you for your interest in contributing!

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/yourusername/wlroots-qt-compositor.git`
3. Create a feature branch: `git checkout -b feature/your-feature`
4. Make your changes
5. Test your changes
6. Commit: `git commit -am 'Add your feature'`
7. Push: `git push origin feature/your-feature`
8. Create a Pull Request

## Code Style

### C Code
- Use 4-space indentation
- Follow existing naming conventions (`comp_` prefix for public API)
- Add comments for complex logic
- Keep functions focused and reasonably sized

### C++ Code
- Follow Qt coding conventions
- Use modern C++ features where appropriate
- Prefer Qt types (QString, QList) in Qt code

### Commit Messages
- Use present tense ("Add feature" not "Added feature")
- Keep first line under 72 characters
- Reference issues where applicable

## Testing

Before submitting:
1. Ensure it builds without warnings
2. Test with `weston-terminal` and other Wayland clients
3. Check that existing functionality still works

## Areas for Contribution

- **Hardware Acceleration**: Implementing DMA-BUF/EGL texture sharing
- **Protocol Support**: Additional Wayland protocols (layer-shell, etc.)
- **Documentation**: Improving docs and examples
- **Testing**: Adding automated tests
- **Bug Fixes**: Fixing issues and edge cases

## Questions?

Feel free to open an issue for discussion before starting major work.
