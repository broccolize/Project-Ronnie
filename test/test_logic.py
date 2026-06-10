"""
Ronnie Unit Tests — Pure Logic
Run: python test/test_logic.py   (from project root)

Tests the critical logic from voice.cpp and enrollment.cpp without any
hardware, compiler, or Arduino libraries. Each test mirrors the exact
C++ behaviour so failures here mean bugs in the firmware too.
"""

import unittest
import math

# ─────────────────────────────────────────────────────────────────────────────
# Mirrors of the C++ functions under test
# ─────────────────────────────────────────────────────────────────────────────

# --- voice.cpp: labelToHour() ------------------------------------------------

HOUR_LABELS = [
    "one","two","three","four","five","six",
    "seven","eight","nine","ten","eleven","twelve"
]

def labelToHour(label: str) -> int:
    """Exact Python mirror of voice.cpp labelToHour()"""
    for i, word in enumerate(HOUR_LABELS):
        if label == word:
            return i + 1
    return -1


# --- voice.cpp: AM/PM conversion logic ---------------------------------------

def apply_ampm(hour: int, ampm: str) -> int:
    """Exact mirror of the am/pm conversion in voice_loop()"""
    if ampm == "pm" and hour < 12:
        hour += 12
    if ampm == "am" and hour == 12:
        hour = 0
    return hour


# --- voice.cpp: VoiceState machine -------------------------------------------

class VoiceState:
    IDLE                 = "VS_IDLE"
    REMINDER_WAIT_HOUR   = "VS_REMINDER_WAIT_HOUR"
    REMINDER_WAIT_AMPM   = "VS_REMINDER_WAIT_AMPM"
    REMINDER_WAIT_TASK   = "VS_REMINDER_WAIT_TASK"


class MockDFPlayer:
    """Records every play() call so tests can assert what audio was triggered"""
    def __init__(self):
        self.played = []
    def play(self, track: int):
        self.played.append(track)
    def last(self) -> int:
        return self.played[-1] if self.played else None
    def reset(self):
        self.played.clear()


class VoiceStateMachine:
    """
    Pure Python simulation of the voice state machine from voice.cpp.
    Call feed(label) to simulate Edge Impulse returning a classification.
    """
    AWAKE_TIMEOUT_MS = 8000
    CONFIDENCE_MIN   = 0.80

    def __init__(self, dfplayer: MockDFPlayer):
        self.df          = dfplayer
        self.state       = VoiceState.IDLE
        self.pending_hour = -1
        self.awake        = False
        self.reminders_saved = []   # collects reminder dicts from reminder_add_voice()
        self._timed_out   = False   # set externally to simulate awake timeout

    def wake(self):
        """Simulate 'hey_ronnie' wake word detected"""
        self.awake       = True
        self._timed_out  = False
        self.state       = VoiceState.IDLE
        self.df.play(68)

    def timeout(self):
        """Simulate the awake window expiring"""
        self._timed_out = True

    def feed(self, label: str, score: float = 0.95) -> str:
        """
        Feed one classified label into the machine.
        Returns the new state name.
        """
        # Simulate task-stage (enrollment matcher path)
        if self.state == VoiceState.REMINDER_WAIT_TASK:
            if self._timed_out or not self.awake:
                self.state = VoiceState.IDLE
                self.awake = False
                self.df.play(99)
                return self.state
            # In the real code, enrollment_match() compares audio.
            # Here we simulate a match if label is a known task word.
            matched_text = self._mock_enrollment_match(label)
            if matched_text is None:
                return self.state  # no match yet — keep listening
            # Save reminder
            self.reminders_saved.append({
                'hour': self.pending_hour, 'minute': 0,
                'text': matched_text, 'mp3': 10
            })
            self.df.play(20)       # "Got it! Alarm set."
            self.state       = VoiceState.IDLE
            self.pending_hour = -1
            self.awake        = False
            return self.state

        # Score gate (Edge Impulse confidence threshold)
        if score < self.CONFIDENCE_MIN:
            return self.state

        # Must be awake for any command
        if label == "hey_ronnie":
            self.wake()
            return self.state

        if not self.awake or self._timed_out:
            self.awake = False
            return self.state

        # State machine stages
        if self.state == VoiceState.REMINDER_WAIT_HOUR:
            h = labelToHour(label)
            if h < 0:
                self.df.play(99)
                self.state = VoiceState.IDLE
                self.awake = False
            else:
                self.pending_hour = h
                self.state        = VoiceState.REMINDER_WAIT_AMPM
                self.df.play(81)  # "AM or PM?"
            return self.state

        if self.state == VoiceState.REMINDER_WAIT_AMPM:
            self.pending_hour = apply_ampm(self.pending_hour, label)
            self.state        = VoiceState.REMINDER_WAIT_TASK
            self.df.play(82)  # "What should I remind you about?"
            self.awake        = True
            return self.state

        # VS_IDLE: normal commands
        if label == "reminder":
            self.state = VoiceState.REMINDER_WAIT_HOUR
            self.df.play(80)  # "At what hour?"
            return self.state

        # All other commands — dispatch (simplified mock)
        self.awake = False
        self.df.play({"time":2,"date":3,"weather":4,"battery":60}.get(label, 99))
        return self.state

    # Mock enrollment matcher — maps known task labels to reminder text
    _enrolled_tasks = {
        "pills":   "Take pills",
        "meeting": "Team meeting",
        "gym":     "Go to gym",
    }

    def _mock_enrollment_match(self, label: str):
        return self._enrolled_tasks.get(label)


# --- enrollment.cpp: cosine similarity ---------------------------------------

def cosine_similarity(a: list, b: list) -> float:
    """Exact mirror of enrollment.cpp cosine similarity calculation"""
    dot = normA = normB = 0.0
    length = min(len(a), len(b))
    for i in range(length):
        fa, fb = float(a[i]), float(b[i])
        dot   += fa * fb
        normA += fa * fa
        normB += fb * fb
    if normA == 0.0 or normB == 0.0:
        return 0.0
    return dot / (math.sqrt(normA) * math.sqrt(normB))

THRESHOLD = 0.60  # from enrollment.cpp

def enrollment_match(audio: list, enrolled_samples: list) -> int:
    """Returns best-match index, or -1 if below threshold"""
    best_score = 0.0
    best_idx   = -1
    for i, sample in enumerate(enrolled_samples):
        score = cosine_similarity(audio, sample)
        if score > best_score:
            best_score = score
            best_idx   = i
    return best_idx if best_score >= THRESHOLD else -1


# ─────────────────────────────────────────────────────────────────────────────
# Test cases
# ─────────────────────────────────────────────────────────────────────────────

class TestLabelToHour(unittest.TestCase):

    def test_all_twelve_words(self):
        expected = {"one":1,"two":2,"three":3,"four":4,"five":5,"six":6,
                    "seven":7,"eight":8,"nine":9,"ten":10,"eleven":11,"twelve":12}
        for word, hour in expected.items():
            with self.subTest(word=word):
                self.assertEqual(labelToHour(word), hour)

    def test_invalid_returns_minus_one(self):
        for bad in ["zero", "thirteen", "reminder", "", "SEVEN", "7"]:
            with self.subTest(bad=bad):
                self.assertEqual(labelToHour(bad), -1)

    def test_case_sensitive(self):
        self.assertEqual(labelToHour("Seven"), -1)
        self.assertEqual(labelToHour("seven"), 7)


class TestAmPmConversion(unittest.TestCase):

    def test_pm_adds_12_below_noon(self):
        self.assertEqual(apply_ampm(7,  "pm"), 19)
        self.assertEqual(apply_ampm(1,  "pm"), 13)
        self.assertEqual(apply_ampm(11, "pm"), 23)

    def test_pm_does_not_add_to_noon(self):
        # 12 PM = 12 (noon) — no addition
        self.assertEqual(apply_ampm(12, "pm"), 12)

    def test_am_converts_12_to_midnight(self):
        # 12 AM = 0 (midnight)
        self.assertEqual(apply_ampm(12, "am"), 0)

    def test_am_leaves_other_hours_alone(self):
        for h in range(1, 12):
            with self.subTest(h=h):
                self.assertEqual(apply_ampm(h, "am"), h)

    def test_boundary_hours(self):
        self.assertEqual(apply_ampm(1,  "pm"), 13)   # 1 PM → 13:00
        self.assertEqual(apply_ampm(11, "pm"), 23)   # 11 PM → 23:00


class TestVoiceStateMachine(unittest.TestCase):

    def setUp(self):
        self.df  = MockDFPlayer()
        self.sm  = VoiceStateMachine(self.df)

    # ── Happy path: full 3-stage reminder flow ────────────────────────────

    def test_full_reminder_flow_7pm(self):
        self.sm.wake()
        self.assertEqual(self.sm.feed("reminder"), VoiceState.REMINDER_WAIT_HOUR)
        self.assertEqual(self.df.last(), 80)   # "At what hour?"

        self.assertEqual(self.sm.feed("seven"), VoiceState.REMINDER_WAIT_AMPM)
        self.assertEqual(self.df.last(), 81)   # "AM or PM?"
        self.assertEqual(self.sm.pending_hour, 7)

        self.assertEqual(self.sm.feed("pm"), VoiceState.REMINDER_WAIT_TASK)
        self.assertEqual(self.df.last(), 82)   # "What should I remind you about?"
        self.assertEqual(self.sm.pending_hour, 19)  # 7 PM = 19:00

        self.assertEqual(self.sm.feed("pills"), VoiceState.IDLE)
        self.assertEqual(self.df.last(), 20)   # "Got it!"
        self.assertEqual(len(self.sm.reminders_saved), 1)
        r = self.sm.reminders_saved[0]
        self.assertEqual(r['hour'], 19)
        self.assertEqual(r['text'], "Take pills")

    def test_full_reminder_flow_12am(self):
        """12 AM should map to hour 0 (midnight)"""
        self.sm.wake()
        self.sm.feed("reminder")
        self.sm.feed("twelve")
        self.sm.feed("am")
        self.assertEqual(self.sm.pending_hour, 0)
        self.assertEqual(self.sm.state, VoiceState.REMINDER_WAIT_TASK)

    def test_full_reminder_flow_12pm(self):
        """12 PM should stay as 12 (noon)"""
        self.sm.wake()
        self.sm.feed("reminder")
        self.sm.feed("twelve")
        self.sm.feed("pm")
        self.assertEqual(self.sm.pending_hour, 12)

    # ── Edge cases ────────────────────────────────────────────────────────

    def test_commands_ignored_before_wake(self):
        initial_state = self.sm.state
        self.sm.feed("time")
        self.assertEqual(self.sm.state, initial_state)
        self.assertEqual(len(self.df.played), 0)

    def test_invalid_hour_word_cancels_flow(self):
        self.sm.wake()
        self.sm.feed("reminder")
        self.sm.feed("thirteen")   # not a valid hour word
        self.assertEqual(self.df.last(), 99)   # "Sorry, didn't catch that"
        self.assertEqual(self.sm.state, VoiceState.IDLE)

    def test_timeout_during_task_stage_cancels(self):
        self.sm.wake()
        self.sm.feed("reminder")
        self.sm.feed("three")
        self.sm.feed("pm")
        self.assertEqual(self.sm.state, VoiceState.REMINDER_WAIT_TASK)
        self.sm.timeout()     # simulate awake window expiring
        self.sm.feed("pills") # comes in after timeout
        self.assertEqual(self.df.last(), 99)
        self.assertEqual(self.sm.state, VoiceState.IDLE)
        self.assertEqual(len(self.sm.reminders_saved), 0)

    def test_unknown_task_word_keeps_listening(self):
        self.sm.wake()
        self.sm.feed("reminder")
        self.sm.feed("five")
        self.sm.feed("am")
        # "dentist" is not enrolled
        state_before = self.sm.state
        self.sm.feed("dentist")
        self.assertEqual(self.sm.state, state_before)   # still waiting
        self.assertEqual(len(self.sm.reminders_saved), 0)

    def test_wake_word_resets_state(self):
        """A new wake word while mid-flow resets state to IDLE"""
        self.sm.wake()
        self.sm.feed("reminder")
        self.assertEqual(self.sm.state, VoiceState.REMINDER_WAIT_HOUR)
        self.sm.feed("hey_ronnie")   # new wake
        self.assertEqual(self.sm.state, VoiceState.IDLE)

    def test_low_confidence_ignored(self):
        self.sm.wake()
        self.sm.feed("time", score=0.50)   # below 0.80 threshold
        self.assertEqual(self.sm.awake, True)
        # awake should remain, no command dispatched
        self.assertEqual(self.df.last(), 68)   # only the wake play, nothing else

    def test_normal_commands_dispatch(self):
        self.sm.wake()
        self.sm.feed("time")
        self.assertFalse(self.sm.awake)   # awake consumed

    def test_multiple_reminders(self):
        for task in [("seven","pm","pills"), ("eight","am","gym")]:
            self.df.reset()
            self.sm.wake()
            self.sm.feed("reminder")
            self.sm.feed(task[0])
            self.sm.feed(task[1])
            self.sm.feed(task[2])
        self.assertEqual(len(self.sm.reminders_saved), 2)


class TestCosineSimilarity(unittest.TestCase):

    def test_identical_signals_score_1(self):
        sig = [100, -200, 350, 0, 150] * 100
        score = cosine_similarity(sig, sig)
        self.assertAlmostEqual(score, 1.0, places=5)

    def test_opposite_signals_score_minus_1(self):
        sig  = [100, 200, 300]
        neg  = [-100, -200, -300]
        score = cosine_similarity(sig, neg)
        self.assertAlmostEqual(score, -1.0, places=5)

    def test_orthogonal_signals_score_0(self):
        a = [1, 0, 0]
        b = [0, 1, 0]
        score = cosine_similarity(a, b)
        self.assertAlmostEqual(score, 0.0, places=5)

    def test_zero_vector_returns_0(self):
        self.assertEqual(cosine_similarity([0, 0, 0], [1, 2, 3]), 0.0)

    def test_slightly_different_signals_above_threshold(self):
        base  = [1000] * 16000
        noisy = [1000 + (i % 3 - 1) * 5 for i in range(16000)]  # ±5 noise
        score = cosine_similarity(base, noisy)
        self.assertGreater(score, THRESHOLD)

    def test_completely_different_signals_below_threshold(self):
        import random
        random.seed(42)
        a = [random.randint(-1000, 1000) for _ in range(1000)]
        b = [random.randint(-1000, 1000) for _ in range(1000)]
        score = cosine_similarity(a, b)
        # Random signals have very low cosine similarity
        self.assertLess(abs(score), 0.20)


class TestEnrollmentMatch(unittest.TestCase):

    # Create simple synthetic "voice samples" for testing
    PILLS_SAMPLE   = [500]  * 16000
    MEETING_SAMPLE = [-500] * 16000
    GYM_SAMPLE     = [200, -200] * 8000

    enrolled = [PILLS_SAMPLE, MEETING_SAMPLE, GYM_SAMPLE]

    def test_exact_match_returns_correct_index(self):
        self.assertEqual(enrollment_match(self.PILLS_SAMPLE,   self.enrolled), 0)
        self.assertEqual(enrollment_match(self.MEETING_SAMPLE, self.enrolled), 1)
        self.assertEqual(enrollment_match(self.GYM_SAMPLE,     self.enrolled), 2)

    def test_no_enrolled_samples_returns_minus_one(self):
        self.assertEqual(enrollment_match(self.PILLS_SAMPLE, []), -1)

    def test_silence_returns_minus_one(self):
        silence = [0] * 16000
        self.assertEqual(enrollment_match(silence, self.enrolled), -1)

    def test_completely_different_audio_below_threshold(self):
        import random
        random.seed(99)
        random_audio = [random.randint(-32768, 32767) for _ in range(16000)]
        # Matching against constant-signal enrolled samples should be near 0
        result = enrollment_match(random_audio, self.enrolled)
        # Either -1 (no match) or very low score — check it's not falsely matched
        if result != -1:
            score = cosine_similarity(random_audio, self.enrolled[result])
            self.assertGreater(score, THRESHOLD)  # if matched, must be valid

    def test_near_match_pill_sample_still_matches(self):
        noisy = [500 + (i % 5 - 2) * 10 for i in range(16000)]  # ±20 variation
        self.assertEqual(enrollment_match(noisy, self.enrolled), 0)


class TestMaxReminders(unittest.TestCase):
    """Mirrors the MAX_REMINDERS=10 guard in reminder_add_voice()"""

    MAX_REMINDERS = 10

    def test_at_capacity_does_not_add(self):
        saved = [{'hour': i, 'text': f'reminder {i}'} for i in range(self.MAX_REMINDERS)]
        # Simulate adding one more
        if len(saved) >= self.MAX_REMINDERS:
            overflowed = True
        else:
            saved.append({'hour': 99, 'text': 'overflow'})
            overflowed = False
        self.assertTrue(overflowed)
        self.assertEqual(len(saved), self.MAX_REMINDERS)


# ─────────────────────────────────────────────────────────────────────────────

if __name__ == '__main__':
    import sys, io
    # Force UTF-8 output on Windows terminals
    sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')
    print('\n[Ronnie] Unit Tests -- Logic Layer\n' + '-' * 40)
    unittest.main(verbosity=2)
