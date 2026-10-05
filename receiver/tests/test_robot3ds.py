import socket
import time
import unittest

from robot3ds.drive import DriveConfig, to_drive_command
from robot3ds.led import OFF, to_led_output
from robot3ds.protocol import FLAG_STOP, PACKET_SIZE, Button, PacketError, build, parse, seq_newer
from robot3ds.receiver import ControllerReceiver


class ProtocolTest(unittest.TestCase):
    def test_roundtrip(self):
        data = build(seq=42, buttons=Button.A | Button.R, circle=(78, -156), cstick=(10, -20),
                     touch=(160, 120), accel=(1, 2, 3), gyro=(-4, -5, -6))
        self.assertEqual(len(data), PACKET_SIZE)
        s = parse(data)
        self.assertEqual(s.seq, 42)
        self.assertTrue(s.pressed(Button.A))
        self.assertTrue(s.pressed(Button.R))
        self.assertFalse(s.pressed(Button.B))
        self.assertTrue(s.touching)
        self.assertEqual((s.touch_x, s.touch_y), (160, 120))
        self.assertAlmostEqual(s.circle[0], 0.5)
        self.assertAlmostEqual(s.circle[1], -1.0)
        self.assertEqual(s.accel, (1, 2, 3))
        self.assertEqual(s.gyro, (-4, -5, -6))
        self.assertEqual(s.pressed_names(), ["A", "R"])

    def test_c_layout(self):
        # protocol.h と同じバイト配置であることを固定値で確認
        data = build(seq=1, buttons=Button.A, circle=(-1, 2))
        self.assertEqual(data[:4], b"R3DS")
        self.assertEqual(data[4], 1)                          # version
        self.assertEqual(data[8:12], b"\x01\x00\x00\x00")     # seq
        self.assertEqual(data[12:16], b"\x01\x00\x00\x00")    # buttons
        self.assertEqual(data[16:20], b"\xff\xff\x02\x00")    # circle

    def test_rejects_bad_packets(self):
        with self.assertRaises(PacketError):
            parse(b"short")
        with self.assertRaises(PacketError):
            parse(b"XXXX" + build()[4:])

    def test_seq_wraparound(self):
        self.assertTrue(seq_newer(1, 0))
        self.assertFalse(seq_newer(0, 1))
        self.assertFalse(seq_newer(5, 5))
        self.assertTrue(seq_newer(2, 0xFFFFFFFE))


class DriveTest(unittest.TestCase):
    cfg = DriveConfig()

    def cmd(self, **kw):
        return to_drive_command(parse(build(**kw)), self.cfg)

    def test_deadman_required(self):
        self.assertTrue(self.cmd(circle=(0, 156)).is_stop)

    def test_forward(self):
        c = self.cmd(buttons=Button.R, circle=(0, 156))
        self.assertAlmostEqual(c.left, 0.5)
        self.assertAlmostEqual(c.right, 0.5)

    def test_turbo(self):
        c = self.cmd(buttons=Button.R | Button.L, circle=(0, 156))
        self.assertAlmostEqual(c.left, 1.0)

    def test_turn_right_spins_left_wheel_forward(self):
        c = self.cmd(buttons=Button.R, circle=(156, 0))
        self.assertGreater(c.left, 0)
        self.assertLess(c.right, 0)

    def test_deadzone(self):
        self.assertTrue(self.cmd(buttons=Button.R, circle=(10, -10)).is_stop)

    def test_dpad(self):
        c = self.cmd(buttons=Button.R | Button.DDOWN)
        self.assertAlmostEqual(c.left, -0.5)
        self.assertAlmostEqual(c.right, -0.5)

    def test_stop_flag_and_no_state(self):
        self.assertTrue(self.cmd(buttons=Button.R, circle=(0, 156), flags=FLAG_STOP).is_stop)
        self.assertTrue(to_drive_command(None, self.cfg).is_stop)

    def test_outputs_are_bounded(self):
        c = self.cmd(buttons=Button.R | Button.L, circle=(156, 156))
        self.assertLessEqual(abs(c.left), 1.0)
        self.assertLessEqual(abs(c.right), 1.0)


class ReceiverTest(unittest.TestCase):
    def setUp(self):
        self.rx = ControllerReceiver("127.0.0.1", 0, timeout=0.2)
        self.tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.dest = ("127.0.0.1", self.rx.port)

    def tearDown(self):
        self.rx.close()
        self.tx.close()

    def send(self, **kw):
        self.tx.sendto(build(**kw), self.dest)

    def test_latest_state_and_failsafe(self):
        self.send(seq=1, buttons=Button.A)
        self.send(seq=2, buttons=Button.B)
        time.sleep(0.05)
        s = self.rx.poll()
        self.assertEqual(s.seq, 2)
        self.assertTrue(s.pressed(Button.B))
        time.sleep(0.3)
        self.assertIsNone(self.rx.poll())

    def test_drops_stale_and_garbage(self):
        self.send(seq=10)
        self.tx.sendto(b"garbage", self.dest)
        self.send(seq=9)
        time.sleep(0.05)
        self.assertEqual(self.rx.poll().seq, 10)

    def test_accepts_restart(self):
        self.send(seq=50000)
        time.sleep(0.02)
        self.rx.poll()
        self.send(seq=0)
        time.sleep(0.02)
        self.assertEqual(self.rx.poll().seq, 0)


class LedTest(unittest.TestCase):
    def out(self, **kw):
        return to_led_output(parse(build(**kw)))

    def test_a_button_lights_led1(self):
        self.assertTrue(self.out(buttons=Button.A).on)
        self.assertFalse(self.out().on)

    def test_touch_sets_brightness(self):
        self.assertAlmostEqual(self.out(touch=(0, 100)).brightness, 0.0)
        self.assertAlmostEqual(self.out(touch=(319, 100)).brightness, 1.0)

    def test_circle_up_sets_brightness_down_is_off(self):
        self.assertAlmostEqual(self.out(circle=(0, 156)).brightness, 1.0)
        self.assertAlmostEqual(self.out(circle=(0, -156)).brightness, 0.0)

    def test_off_when_disconnected_or_stopped(self):
        self.assertEqual(to_led_output(None), OFF)
        self.assertEqual(self.out(buttons=Button.A, touch=(300, 0), flags=FLAG_STOP), OFF)


if __name__ == "__main__":
    unittest.main()
