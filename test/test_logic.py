"""
Ronnie Unit Tests — Pure Logic (v8)
Run: python test/test_logic.py   (from project root)

Tests the critical logic from the v8 firmware without any hardware, compiler,
or Arduino libraries. Each mirror below reproduces the exact C++ behaviour so a
failure here means a bug in the firmware too.

Mirrored from:
  - commands.cpp : transcript parsers + intent routing (the v8 rewrite)
  - voice.cpp    : 2-window wake-streak confirmation
  - stt.cpp      : 44-byte WAV header
  - reminder.cpp : MAX_REMINDERS guard
"""

import unittest
import struct

# ─────────────────────────────────────────────────────────────────────────────
# commands.cpp — text parsers
# ─────────────────────────────────────────────────────────────────────────────

ONES = [
    "zero", "one", "two", "three", "four", "five", "six", "seven", "eight",
    "nine", "ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen",
    "sixteen", "seventeen", "eighteen", "nineteen",
]
TENS = {"twenty": 20, "thirty": 30, "forty": 40, "fifty": 50}


def one_word_number(w: str) -> int:
    """Mirror of commands.cpp oneWordNumber()."""
    if w in ONES:
        return ONES.index(w)
    return TENS.get(w, -1)


def first_digits(s: str) -> int:
    """Mirror of commands.cpp firstDigits() — first run of digits, else -1."""
    i, n = 0, len(s)
    while i < n and not s[i].isdigit():
        i += 1
    if i >= n:
        return -1
    v = 0
    while i < n and s[i].isdigit():
        v = v * 10 + (ord(s[i]) - ord("0"))
        i += 1
    return v


def words_to_number(s: str) -> int:
    """Mirror of commands.cpp wordsToNumber() — first number, combining 'twenty five'."""
    tokens = s.split()
    for idx, tok in enumerate(tokens):
        v = one_word_number(tok)
        if v >= 0:
            if v % 10 == 0 and v >= 20:
                if idx + 1 < len(tokens):
                    v2 = one_word_number(tokens[idx + 1])
                    if 1 <= v2 <= 9:
                        return v + v2
            return v
    return -1


def parse_number(s: str) -> int:
    """Mirror of commands.cpp parseNumber() — digits preferred, then words."""
    d = first_digits(s)
    if d >= 0:
        return d
    return words_to_number(s)


def parse_clock_time(s: str):
    """Mirror of commands.cpp parseClockTime(). Returns (hour, minute, valid)."""
    hour, minute = -1, 0
    colon = s.find(":")
    if colon > 0:
        hs = colon - 1
        while hs > 0 and s[hs - 1].isdigit():
            hs -= 1
        hour = int(s[hs:colon]) if s[hs:colon].isdigit() else 0
        me = colon + 1
        while me < len(s) and s[me].isdigit():
            me += 1
        minute = int(s[colon + 1:me]) if s[colon + 1:me].isdigit() else 0
    else:
        h = parse_number(s)
        if h < 0:
            return (-1, 0, False)
        hour, minute = h, 0

    pm = ("pm" in s) or ("p m" in s)
    am = ("am" in s) or ("a m" in s)
    if pm and hour < 12:
        hour += 12
    if am and hour == 12:
        hour = 0

    valid = (0 <= hour <= 23) and (0 <= minute <= 59)
    return (hour, minute, valid)


def parse_timer_duration(s: str) -> int:
    """Mirror of commands.cpp parseTimerDuration() — seconds, defaults to minutes."""
    num = parse_number(s)
    if num <= 0:
        return 0
    if "hour" in s:
        return num * 3600
    if "second" in s or "sec" in s:
        return num
    return num * 60


def extract_reminder_text(orig: str) -> str:
    """Mirror of commands.cpp extractReminderText() (operates on original case)."""
    low = orig.lower()
    i = low.find(" to ")
    if i >= 0:
        return orig[i + 4:].strip()
    i = low.find(" about ")
    if i >= 0:
        return orig[i + 7:].strip()
    return orig.strip()


# ─────────────────────────────────────────────────────────────────────────────
# commands.cpp — intent routing (handleCommand)
# ─────────────────────────────────────────────────────────────────────────────

def route_intent(transcript: str, stopwatch_running: bool = False):
    """
    Mirror of the v8 handleCommand() if/else chain. Returns a tuple describing
    the action, instead of calling DFPlayer/handlers. 'error99' = play(99).
    """
    s = transcript.lower()

    if "time" in s and "timer" not in s:
        return ("time",)
    elif "date" in s:
        return ("date",)
    elif "weather" in s:
        return ("weather",)
    elif "alarm" in s:
        h, m, valid = parse_clock_time(s)
        return ("alarm", h, m) if valid else ("error99",)
    elif "timer" in s:
        secs = parse_timer_duration(s)
        return ("timer", secs) if secs > 0 else ("error99",)
    elif "stopwatch" in s:
        if "stop" in s and stopwatch_running:
            return ("stopwatch_stop",)
        return ("stopwatch_start",)
    elif "remind" in s:
        h, m, valid = parse_clock_time(s)
        if not valid:
            return ("error99",)
        return ("reminder", h, m, extract_reminder_text(transcript))
    elif "good morning" in s:
        return ("good_morning",)
    elif "hungry" in s or "battery" in s:
        return ("battery",)
    else:
        return ("error99",)


# ─────────────────────────────────────────────────────────────────────────────
# voice.cpp — wake-streak confirmation (2 consecutive positive windows)
# ─────────────────────────────────────────────────────────────────────────────

class WakeDetector:
    """Mirror of the wakeStreak logic in voice_loop()."""
    THRESHOLD = 0.80
    STREAK_MIN = 2

    def __init__(self):
        self.streak = 0

    def feed(self, label: str, score: float) -> bool:
        """Returns True exactly when a wake fires (and resets the streak)."""
        if label == "hey_ronnie" and score >= self.THRESHOLD:
            self.streak += 1
        else:
            self.streak = 0
            return False
        if self.streak >= self.STREAK_MIN:
            self.streak = 0
            return True
        return False


# ─────────────────────────────────────────────────────────────────────────────
# stt.cpp — WAV header
# ─────────────────────────────────────────────────────────────────────────────

def build_wav_header(data_len: int) -> bytes:
    """Mirror of stt.cpp writeWavHeader(): 16 kHz, 16-bit, mono, 44 bytes."""
    return b"".join([
        b"RIFF",
        struct.pack("<I", 36 + data_len),
        b"WAVE",
        b"fmt ",
        struct.pack("<I", 16),
        struct.pack("<H", 1),       # PCM
        struct.pack("<H", 1),       # mono
        struct.pack("<I", 16000),   # sample rate
        struct.pack("<I", 32000),   # byte rate
        struct.pack("<H", 2),       # block align
        struct.pack("<H", 16),      # bits per sample
        b"data",
        struct.pack("<I", data_len),
    ])


# ─────────────────────────────────────────────────────────────────────────────
# Test cases
# ─────────────────────────────────────────────────────────────────────────────

class TestNumberParsing(unittest.TestCase):

    def test_digits(self):
        self.assertEqual(parse_number("for 7 pm"), 7)
        self.assertEqual(parse_number("set a timer for 30 minutes"), 30)
        self.assertEqual(first_digits("no digits here"), -1)

    def test_word_numbers(self):
        self.assertEqual(words_to_number("seven"), 7)
        self.assertEqual(words_to_number("twelve"), 12)
        self.assertEqual(words_to_number("twenty"), 20)

    def test_compound_tens(self):
        self.assertEqual(words_to_number("twenty five"), 25)
        self.assertEqual(words_to_number("forty seven"), 47)
        self.assertEqual(words_to_number("fifty nine"), 59)

    def test_tens_not_followed_by_one_to_nine(self):
        self.assertEqual(words_to_number("thirty minutes"), 30)
        self.assertEqual(words_to_number("twenty zero"), 20)  # zero is not 1..9

    def test_digits_preferred_over_words(self):
        self.assertEqual(parse_number("seven 9"), 9)

    def test_no_number(self):
        self.assertEqual(parse_number("hello there"), -1)


class TestClockTime(unittest.TestCase):

    def test_hour_with_ampm(self):
        self.assertEqual(parse_clock_time("set an alarm for 7 pm"), (19, 0, True))
        self.assertEqual(parse_clock_time("set an alarm for 7 am"), (7, 0, True))

    def test_colon_time(self):
        self.assertEqual(parse_clock_time("alarm for 6:30 am"), (6, 30, True))
        self.assertEqual(parse_clock_time("alarm for 6:30 pm"), (18, 30, True))

    def test_word_hour(self):
        self.assertEqual(parse_clock_time("set an alarm for seven pm"), (19, 0, True))

    def test_bare_hour_no_ampm(self):
        self.assertEqual(parse_clock_time("set an alarm for 7"), (7, 0, True))

    def test_noon_and_midnight(self):
        self.assertEqual(parse_clock_time("alarm for 12 pm"), (12, 0, True))  # noon stays 12
        self.assertEqual(parse_clock_time("alarm for 12 am"), (0, 0, True))   # midnight → 0

    def test_no_time_is_invalid(self):
        self.assertEqual(parse_clock_time("set an alarm"), (-1, 0, False))


class TestTimerDuration(unittest.TestCase):

    def test_minutes_default(self):
        self.assertEqual(parse_timer_duration("set a timer for five minutes"), 300)
        self.assertEqual(parse_timer_duration("timer for 10 minutes"), 600)

    def test_hours(self):
        self.assertEqual(parse_timer_duration("set a timer for two hours"), 7200)

    def test_seconds(self):
        self.assertEqual(parse_timer_duration("timer for 30 seconds"), 30)

    def test_no_unit_defaults_to_minutes(self):
        self.assertEqual(parse_timer_duration("timer for 5"), 300)

    def test_missing_duration_is_zero(self):
        self.assertEqual(parse_timer_duration("set a timer"), 0)


class TestReminderText(unittest.TestCase):

    def test_text_after_to(self):
        self.assertEqual(
            extract_reminder_text("remind me at 7 PM to take my pills"),
            "take my pills",
        )

    def test_text_after_about(self):
        self.assertEqual(
            extract_reminder_text("remind me at 8 am about the meeting"),
            "the meeting",
        )

    def test_fallback_to_whole_string(self):
        self.assertEqual(extract_reminder_text("remind me later"), "remind me later")


class TestIntentRouting(unittest.TestCase):

    def test_time_not_timer(self):
        self.assertEqual(route_intent("what's the time"), ("time",))
        self.assertEqual(route_intent("set a timer for 5 minutes"), ("timer", 300))

    def test_date(self):
        self.assertEqual(route_intent("what's the date"), ("date",))

    def test_weather(self):
        self.assertEqual(route_intent("how's the weather"), ("weather",))

    def test_alarm(self):
        self.assertEqual(route_intent("set an alarm for 7 pm"), ("alarm", 19, 0))

    def test_alarm_without_time_errors(self):
        self.assertEqual(route_intent("set an alarm"), ("error99",))

    def test_stopwatch_start_and_stop(self):
        self.assertEqual(route_intent("start the stopwatch"), ("stopwatch_start",))
        self.assertEqual(
            route_intent("stop the stopwatch", stopwatch_running=True),
            ("stopwatch_stop",),
        )
        # "stop" but not running → start (mirrors the C++ guard)
        self.assertEqual(
            route_intent("stop the stopwatch", stopwatch_running=False),
            ("stopwatch_start",),
        )

    def test_reminder(self):
        self.assertEqual(
            route_intent("remind me at 7 pm to take my pills"),
            ("reminder", 19, 0, "take my pills"),
        )

    def test_reminder_without_time_errors(self):
        self.assertEqual(route_intent("remind me to do stuff"), ("error99",))

    def test_good_morning(self):
        self.assertEqual(route_intent("good morning"), ("good_morning",))

    def test_battery(self):
        self.assertEqual(route_intent("are you hungry"), ("battery",))
        self.assertEqual(route_intent("what's the battery level"), ("battery",))

    def test_unknown(self):
        self.assertEqual(route_intent("tell me a joke"), ("error99",))


class TestWakeDetector(unittest.TestCase):

    def test_requires_two_consecutive_windows(self):
        wd = WakeDetector()
        self.assertFalse(wd.feed("hey_ronnie", 0.95))  # 1st window — no fire
        self.assertTrue(wd.feed("hey_ronnie", 0.95))   # 2nd window — fires

    def test_streak_resets_on_miss(self):
        wd = WakeDetector()
        self.assertFalse(wd.feed("hey_ronnie", 0.95))
        self.assertFalse(wd.feed("noise", 0.99))        # resets the streak
        self.assertFalse(wd.feed("hey_ronnie", 0.95))   # back to 1
        self.assertTrue(wd.feed("hey_ronnie", 0.95))    # now 2 → fires

    def test_below_threshold_does_not_count(self):
        wd = WakeDetector()
        self.assertFalse(wd.feed("hey_ronnie", 0.50))
        self.assertFalse(wd.feed("hey_ronnie", 0.79))
        self.assertEqual(wd.streak, 0)

    def test_noise_never_fires(self):
        wd = WakeDetector()
        for _ in range(10):
            self.assertFalse(wd.feed("noise", 0.99))


class TestWavHeader(unittest.TestCase):

    def test_length_and_fixed_fields(self):
        h = build_wav_header(0)
        self.assertEqual(len(h), 44)
        self.assertEqual(h[0:4], b"RIFF")
        self.assertEqual(h[8:12], b"WAVE")
        self.assertEqual(h[36:40], b"data")
        # 16 kHz, mono, 16-bit
        self.assertEqual(struct.unpack("<I", h[24:28])[0], 16000)
        self.assertEqual(struct.unpack("<H", h[22:24])[0], 1)
        self.assertEqual(struct.unpack("<H", h[34:36])[0], 16)
        self.assertEqual(struct.unpack("<I", h[28:32])[0], 32000)  # byte rate

    def test_sizes_patched_with_data_len(self):
        n = 32000  # ~1 s of audio
        h = build_wav_header(n)
        self.assertEqual(struct.unpack("<I", h[4:8])[0], 36 + n)    # ChunkSize
        self.assertEqual(struct.unpack("<I", h[40:44])[0], n)        # Subchunk2Size


class TestMaxReminders(unittest.TestCase):
    """Mirrors the MAX_REMINDERS=10 guard in reminder_add_voice()."""

    MAX_REMINDERS = 10

    def test_at_capacity_does_not_add(self):
        saved = [{"hour": i, "text": f"reminder {i}"} for i in range(self.MAX_REMINDERS)]
        if len(saved) >= self.MAX_REMINDERS:
            overflowed = True
        else:
            saved.append({"hour": 99, "text": "overflow"})
            overflowed = False
        self.assertTrue(overflowed)
        self.assertEqual(len(saved), self.MAX_REMINDERS)


# ─────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    import sys, io
    # Force UTF-8 output on Windows terminals
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
    print("\n[Ronnie] Unit Tests -- Logic Layer (v8)\n" + "-" * 40)
    unittest.main(verbosity=2)
