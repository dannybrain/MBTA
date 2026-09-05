#pragma once

#include <stddef.h>

struct StandbyQuote {
    const char *text;
    const char *author;
};

static const StandbyQuote kStandbyQuotes[] = {
    {"The journey of a thousand miles begins with one step.", "Lao Tzu"},
    {"Knowing others is wisdom; knowing yourself is enlightenment.", "Lao Tzu"},
    {"It is not that we have a short time to live, but that we waste much of it.",
     "Seneca"},
    {"We suffer more often in imagination than in reality.", "Seneca"},
    {"The unexamined life is not worth living.", "Socrates"},
    {"Happiness depends upon ourselves.", "Aristotle"},
    {"The only true wisdom is in knowing you know nothing.", "Socrates"},
    {"He who has a why to live can bear almost any how.", "Nietzsche"},
    {"In the midst of winter, I found there was, within me, an invincible summer.",
     "Camus"},
    {"The art of being wise is the art of knowing what to overlook.", "William James"},
    {"Do not dwell in the past; do not dream of the future. Concentrate the mind on "
     "the present moment.",
     "Buddha"},
    {"What we think, we become.", "Buddha"},
    {"Patience is bitter, but its fruit is sweet.", "Aristotle"},
    {"The mind is everything. What you think you become.", "Buddha"},
    {"To improve is to change; to be perfect is to change often.", "Churchill"},
    {"Life can only be understood backwards; but it must be lived forwards.", "Kierkegaard"},
    {"The secret of change is to focus all of your energy not on fighting the old, "
     "but on building the new.",
     "Socrates"},
    {"Simplicity is the ultimate sophistication.", "Leonardo da Vinci"},
    {"Well begun is half done.", "Aristotle"},
    {"The best time to plant a tree was twenty years ago. The second best time is now.",
     "Chinese proverb"},
};

static constexpr size_t kStandbyQuoteCount =
    sizeof(kStandbyQuotes) / sizeof(kStandbyQuotes[0]);

inline const StandbyQuote &standbyQuoteAt(size_t index) {
    return kStandbyQuotes[index % kStandbyQuoteCount];
}
