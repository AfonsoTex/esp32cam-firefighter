# User coding preferences

- Never use the ternary operator (`condition ? a : b`) in code written or edited for this user. Use explicit if/else blocks.
- Prefer straightforward, easy-to-read code even when it takes more lines. Avoid compact expressions and unnecessary abstractions.
- Preserve the user's existing code and structure. When asked to complete it, add the missing parts without unrelated rewrites or refactoring.
- When guiding the user step by step, explain and add only the current small step. Complete an entire file only when explicitly requested.
- Write source-code comments in English. Explain the work to the user in Portuguese.
- Keep constexpr for the existing servo constants; the user asked for an explanation, not replacement with const.
