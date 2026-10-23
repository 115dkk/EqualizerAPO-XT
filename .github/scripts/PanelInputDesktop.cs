// Physical desktop input for the panel gate. Ported from the MIT-licensed
// vst-audio test desktop helper; no test-window runtime is imported.
// Keep this file C# 5: Windows PowerShell 5.1 compiles it with .NET Framework.
/*
 * Ported portions: Copyright (c) 2026 115dkk (MIT License).
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Automation;

namespace PanelInputGate
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect
    {
        public int Left, Top, Right, Bottom;
        public int Width { get { return Right - Left; } }
        public int Height { get { return Bottom - Top; } }
        public bool Contains(int x, int y)
        {
            return x >= Left && x < Right && y >= Top && y < Bottom;
        }
        public override string ToString()
        {
            return String.Format("({0},{1}) {2}x{3}", Left, Top, Width, Height);
        }
    }

    public sealed class Window
    {
        public IntPtr Handle;
        public string Class, Title;
        public uint ProcessId;
        public Rect Rect;
        public override string ToString()
        {
            return String.Format("HWND={0} class='{1}' title='{2}' pid={3} rect={4}",
                Handle, Class, Title, ProcessId, Rect);
        }
    }

    public static class Desktop
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct Point { public int X, Y; }
        [StructLayout(LayoutKind.Sequential)]
        private struct MouseInput
        {
            public int Dx, Dy;
            public uint MouseData, Flags, Time;
            public UIntPtr ExtraInfo;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct KeyboardInput
        {
            public ushort VirtualKey, Scan;
            public uint Flags, Time;
            public UIntPtr ExtraInfo;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct HardwareInput
        {
            public uint Message;
            public ushort Low, High;
        }
        // The union's largest member is MOUSEINPUT (32 bytes on x64, 24 on
        // x86). Sequential INPUT inserts x64 padding after Type: 40/28 bytes.
        [StructLayout(LayoutKind.Explicit)]
        private struct InputUnion
        {
            [FieldOffset(0)] public MouseInput Mouse;
            [FieldOffset(0)] public KeyboardInput Keyboard;
            [FieldOffset(0)] public HardwareInput Hardware;
        }
        [StructLayout(LayoutKind.Sequential)]
        private struct Input { public uint Type; public InputUnion Data; }
        private delegate bool EnumCallback(IntPtr hwnd, IntPtr state);
        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumCallback callback, IntPtr state);
        [DllImport("user32.dll")] private static extern bool EnumChildWindows(IntPtr hwnd, EnumCallback callback, IntPtr state);
        [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr hwnd);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool GetWindowRect(IntPtr hwnd, out Rect rect);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool GetClientRect(IntPtr hwnd, out Rect rect);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool ClientToScreen(IntPtr hwnd, ref Point point);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hwnd, StringBuilder text, int count);
        [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetWindowText(IntPtr hwnd, StringBuilder text, int count);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
        [DllImport("user32.dll")] private static extern IntPtr WindowFromPoint(Point point);
        [DllImport("user32.dll")] private static extern IntPtr GetAncestor(IntPtr hwnd, uint flags);
        [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr hwnd);
        [DllImport("user32.dll")] private static extern int GetSystemMetrics(int index);
        [DllImport("user32.dll", SetLastError = true)] private static extern uint SendInput(uint count, Input[] input, int size);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool GetCursorPos(out Point point);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool PostMessage(IntPtr hwnd, uint message, IntPtr wparam, IntPtr lparam);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool SetProcessDpiAwarenessContext(IntPtr context);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool SetProcessDPIAware();
        [DllImport("user32.dll")] private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

        public static void Initialize()
        {
            int expected = IntPtr.Size == 8 ? 40 : 28;
            if (Marshal.SizeOf(typeof(Input)) != expected)
                throw new InvalidOperationException("Unexpected INPUT ABI size");
            try
            {
                if (SetProcessDpiAwarenessContext(new IntPtr(-4))) return;
                // PowerShell may have selected a process context already. A
                // thread override still keeps UIA, screen and HWND pixels aligned.
                if (SetThreadDpiAwarenessContext(new IntPtr(-4)) != IntPtr.Zero) return;
            }
            catch (EntryPointNotFoundException) { }
            if (!SetProcessDPIAware())
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Cannot establish physical-pixel DPI awareness");
        }

        public static Window Describe(IntPtr hwnd)
        {
            uint pid;
            GetWindowThreadProcessId(hwnd, out pid);
            StringBuilder cls = new StringBuilder(256), title = new StringBuilder(1024);
            GetClassName(hwnd, cls, cls.Capacity);
            GetWindowText(hwnd, title, title.Capacity);
            Rect rect;
            GetWindowRect(hwnd, out rect);
            return new Window { Handle = hwnd, Class = cls.ToString(), Title = title.ToString(), ProcessId = pid, Rect = rect };
        }

        public static Window[] TopWindows(uint pid)
        {
            List<Window> windows = new List<Window>();
            EnumWindows(delegate(IntPtr hwnd, IntPtr state)
            {
                Window window = Describe(hwnd);
                if (window.ProcessId == pid && IsWindowVisible(hwnd)) windows.Add(window);
                return true;
            }, IntPtr.Zero);
            return windows.ToArray();
        }

        public static Window FindChildClass(IntPtr top, string prefix)
        {
            Window found = null;
            EnumChildWindows(top, delegate(IntPtr hwnd, IntPtr state)
            {
                Window window = Describe(hwnd);
                if (IsWindowVisible(hwnd) && window.Class.StartsWith(prefix, StringComparison.Ordinal))
                {
                    found = window;
                    return false;
                }
                return true;
            }, IntPtr.Zero);
            return found;
        }

        public static Rect ClientRect(IntPtr hwnd)
        {
            Rect rect;
            Point origin = new Point();
            if (!GetClientRect(hwnd, out rect) || !ClientToScreen(hwnd, ref origin))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "Plugin client rectangle");
            return new Rect { Left = origin.X, Top = origin.Y, Right = origin.X + rect.Width, Bottom = origin.Y + rect.Height };
        }

        public static Rect? FindNamed(IntPtr top, string name, bool buttonOnly)
        {
            try
            {
                AutomationElement root = AutomationElement.FromHandle(top);
                if (root == null) return null;
                Condition condition = new PropertyCondition(AutomationElement.NameProperty, name);
                if (buttonOnly)
                    condition = new AndCondition(condition, new PropertyCondition(AutomationElement.ControlTypeProperty, ControlType.Button));
                AutomationElementCollection matches = root.FindAll(TreeScope.Descendants, condition);
                foreach (AutomationElement element in matches)
                {
                    if (element.Current.IsOffscreen) continue;
                    System.Windows.Rect bounds = element.Current.BoundingRectangle;
                    if (bounds.IsEmpty) continue;
                    Rect rect = new Rect { Left = (int)Math.Ceiling(bounds.Left), Top = (int)Math.Ceiling(bounds.Top),
                        Right = (int)Math.Floor(bounds.Right), Bottom = (int)Math.Floor(bounds.Bottom) };
                    Rect desktop = ScreenRect();
                    if (rect.Width > 0 && rect.Height > 0 && desktop.Contains(rect.Left, rect.Top) && desktop.Contains(rect.Right - 1, rect.Bottom - 1))
                        return rect;
                }
                return null;
            }
            catch (ElementNotAvailableException) { return null; }
        }

        public static Rect ScreenRect()
        {
            int x = GetSystemMetrics(76), y = GetSystemMetrics(77);
            return new Rect { Left = x, Top = y, Right = x + GetSystemMetrics(78), Bottom = y + GetSystemMetrics(79) };
        }

        private static void Send(int dx, int dy, uint flags)
        {
            Input input = new Input();
            input.Data.Mouse = new MouseInput { Dx = dx, Dy = dy, Flags = flags };
            uint sent = SendInput(1, new Input[] { input }, Marshal.SizeOf(typeof(Input)));
            if (sent != 1)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "SendInput returned " + sent + "/1 (desktop/UIPI)");
        }

        private static Window Hit(int x, int y)
        {
            return Describe(GetAncestor(WindowFromPoint(new Point { X = x, Y = y }), 2));
        }

        public static void BringToFront(uint pid, int x, int y)
        {
            Window target = null;
            foreach (Window window in TopWindows(pid))
                if (window.Rect.Contains(x, y) && (target == null || (long)window.Rect.Width * window.Rect.Height < (long)target.Rect.Width * target.Rect.Height))
                    target = window;
            if (target == null) throw new InvalidOperationException("No target window for pid=" + pid + " at " + x + "," + y);
            Send(0, 0, 0x0001);
            SetForegroundWindow(target.Handle);
            Thread.Sleep(40);
            Window covering = Hit(x, y);
            if (covering.ProcessId != pid)
                throw new InvalidOperationException("Input blocked at " + x + "," + y + " by " + covering);
        }

        private static void CheckHit(uint pid, int x, int y)
        {
            if (Hit(x, y).ProcessId != pid) BringToFront(pid, x, y);
            Window covering = Hit(x, y);
            if (covering.ProcessId != pid)
                throw new InvalidOperationException("Input blocked at " + x + "," + y + " by " + covering);
        }

        private static void Pointer(uint pid, int x, int y)
        {
            Rect desktop = ScreenRect();
            if (desktop.Width < 2 || desktop.Height < 2 || !desktop.Contains(x, y))
                throw new InvalidOperationException("Input point outside virtual desktop: " + desktop);
            CheckHit(pid, x, y);
            int dx = (int)((long)(x - desktop.Left) * 65535 / (desktop.Width - 1));
            int dy = (int)((long)(y - desktop.Top) * 65535 / (desktop.Height - 1));
            Send(dx, dy, 0x8000 | 0x4000 | 0x0001);
            Stopwatch wait = Stopwatch.StartNew();
            while (true)
            {
                Thread.Sleep(40);
                Point actual;
                if (!GetCursorPos(out actual)) throw new Win32Exception(Marshal.GetLastWin32Error());
                if (Math.Abs(actual.X - x) <= 1 && Math.Abs(actual.Y - y) <= 1) break;
                if (wait.ElapsedMilliseconds >= 2000)
                    throw new InvalidOperationException("OS did not deliver mouse movement to " + x + "," + y);
            }
        }

        public static void Click(uint pid, int x, int y)
        {
            BringToFront(pid, x, y);
            Pointer(pid, x, y);
            CheckHit(pid, x, y);
            try
            {
                Send(0, 0, 0x0002);
                Thread.Sleep(40);
            }
            finally { Send(0, 0, 0x0004); }
            Thread.Sleep(40);
        }

        public static void Drag(uint pid, int fromX, int fromY, int toX, int toY, int steps)
        {
            if (steps <= 0) throw new ArgumentOutOfRangeException("steps");
            BringToFront(pid, fromX, fromY);
            Pointer(pid, fromX, fromY);
            CheckHit(pid, fromX, fromY);
            try
            {
                Send(0, 0, 0x0002);
                Thread.Sleep(40);
                for (int i = 1; i <= steps; ++i)
                    Pointer(pid, fromX + (int)((long)(toX - fromX) * i / steps), fromY + (int)((long)(toY - fromY) * i / steps));
                CheckHit(pid, toX, toY);
            }
            finally { Send(0, 0, 0x0004); }
            Thread.Sleep(40);
        }

        public static void Close(IntPtr hwnd)
        {
            if (!PostMessage(hwnd, 0x0010, IntPtr.Zero, IntPtr.Zero))
                throw new Win32Exception(Marshal.GetLastWin32Error(), "WM_CLOSE");
        }

        public static byte[] Capture(Rect rect, string pngPath)
        {
            Rect desktop = ScreenRect();
            if (rect.Width <= 0 || rect.Height <= 0 || !desktop.Contains(rect.Left, rect.Top) || !desktop.Contains(rect.Right - 1, rect.Bottom - 1))
                throw new InvalidOperationException("Capture rectangle outside desktop: " + rect);
            using (Bitmap bitmap = new Bitmap(rect.Width, rect.Height, PixelFormat.Format32bppArgb))
            {
                using (Graphics graphics = Graphics.FromImage(bitmap))
                    graphics.CopyFromScreen(rect.Left, rect.Top, 0, 0, bitmap.Size, CopyPixelOperation.SourceCopy);
                if (!String.IsNullOrEmpty(pngPath)) bitmap.Save(pngPath, ImageFormat.Png);
                byte[] bytes = new byte[checked(rect.Width * rect.Height * 4)];
                BitmapData data = bitmap.LockBits(new Rectangle(0, 0, rect.Width, rect.Height), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
                try
                {
                    for (int row = 0; row < rect.Height; ++row)
                        Marshal.Copy(IntPtr.Add(data.Scan0, row * data.Stride), bytes, row * rect.Width * 4, rect.Width * 4);
                }
                finally { bitmap.UnlockBits(data); }
                // GDI alpha is not meaningful. Stabilize it before pixel comparison.
                for (int i = 3; i < bytes.Length; i += 4) bytes[i] = 255;
                return bytes;
            }
        }

        public static int ChangedPixels(byte[] first, byte[] second)
        {
            if (first.Length != second.Length || first.Length % 4 != 0)
                throw new InvalidDataException("Capture dimensions changed");
            int count = 0;
            for (int i = 0; i < first.Length; i += 4)
                if (first[i] != second[i] || first[i + 1] != second[i + 1] || first[i + 2] != second[i + 2] || first[i + 3] != second[i + 3]) ++count;
            return count;
        }
    }

    // Binary/audio work lives here to avoid hundreds of thousands of boxed
    // PowerShell pipeline objects. All formats are explicitly little endian.
    public static class Audio
    {
        public static void WriteSignal(string path, int rate, int channels, int frames)
        {
            if (channels != 2) throw new ArgumentException("The seeded signal has two channels");
            int size = checked(frames * channels * 4);
            double seconds = (double)frames / rate;
            uint[] seeds = new uint[] { 0x12345678, 0x9abcdef0 };
            using (BinaryWriter writer = new BinaryWriter(File.Create(path)))
            {
                writer.Write(Encoding.ASCII.GetBytes("RIFF")); writer.Write(size + 48);
                writer.Write(Encoding.ASCII.GetBytes("WAVEfmt ")); writer.Write(16);
                writer.Write((ushort)3); writer.Write((ushort)channels); writer.Write(rate);
                writer.Write(rate * channels * 4); writer.Write((ushort)(channels * 4)); writer.Write((ushort)32);
                writer.Write(Encoding.ASCII.GetBytes("fact")); writer.Write(4); writer.Write(frames);
                writer.Write(Encoding.ASCII.GetBytes("data")); writer.Write(size);
                double log = Math.Log(1000.0);
                for (int i = 0; i < frames; ++i)
                {
                    double t = (double)i / rate;
                    double phase = 2 * Math.PI * 20 * seconds / log * (Math.Exp(t * log / seconds) - 1);
                    for (int ch = 0; ch < channels; ++ch)
                    {
                        seeds[ch] = unchecked(seeds[ch] * 1664525u + 1013904223u);
                        double noise = (double)seeds[ch] / UInt32.MaxValue * 2 - 1;
                        writer.Write((float)(Math.Sin(phase) * Math.Pow(10, -18.0 / 20) + noise * Math.Pow(10, -30.0 / 20)));
                    }
                }
            }
        }

        public static byte[] ReadPcm16(string path, int rate, int channels, int frames)
        {
            using (BinaryReader reader = new BinaryReader(File.OpenRead(path)))
            {
                if (reader.BaseStream.Length < 12 || Encoding.ASCII.GetString(reader.ReadBytes(4)) != "RIFF")
                    throw new InvalidDataException("Not a RIFF WAV: " + path);
                long end = 8L + reader.ReadUInt32();
                if (end != reader.BaseStream.Length || Encoding.ASCII.GetString(reader.ReadBytes(4)) != "WAVE")
                    throw new InvalidDataException("Invalid RIFF size/type: " + path);
                bool fmt = false;
                byte[] data = null;
                while (reader.BaseStream.Position < end)
                {
                    if (end - reader.BaseStream.Position < 8) throw new InvalidDataException("Truncated RIFF chunk header");
                    string id = Encoding.ASCII.GetString(reader.ReadBytes(4));
                    uint size = reader.ReadUInt32();
                    long start = reader.BaseStream.Position, stop = start + size, padded = stop + (size & 1);
                    if (padded > end) throw new InvalidDataException("Truncated RIFF chunk/padding");
                    if (id == "fmt ")
                    {
                        if (fmt || size < 16) throw new InvalidDataException("Invalid/duplicate WAV fmt");
                        ushort tag = reader.ReadUInt16(), count = reader.ReadUInt16();
                        uint frequency = reader.ReadUInt32(), byteRate = reader.ReadUInt32();
                        ushort block = reader.ReadUInt16(), bits = reader.ReadUInt16();
                        if (tag != 1 || count != channels || frequency != rate || bits != 16 || block != channels * 2 || byteRate != rate * channels * 2)
                            throw new InvalidDataException("Expected PCM16, " + rate + " Hz, " + channels + " channels");
                        fmt = true;
                    }
                    else if (id == "data")
                    {
                        if (data != null)
                            throw new InvalidDataException("Duplicate WAV data chunk");
                        if (size != checked(frames * channels * 2))
                            throw new InvalidDataException("WAV data holds " + size / (channels * 2) + " frames; expected " + frames);
                        data = reader.ReadBytes((int)size);
                    }
                    reader.BaseStream.Position = padded;
                }
                if (!fmt || data == null) throw new InvalidDataException("Missing WAV fmt/data");
                return data;
            }
        }

        public static int MaxDifference(byte[] first, byte[] second)
        {
            if (first.Length != second.Length || first.Length % 2 != 0)
                throw new InvalidDataException("PCM sample counts differ");
            int max = 0;
            for (int i = 0; i < first.Length; i += 2)
            {
                int a = (short)(first[i] | first[i + 1] << 8), b = (short)(second[i] | second[i + 1] << 8);
                max = Math.Max(max, Math.Abs(a - b));
            }
            return max;
        }
    }
}
