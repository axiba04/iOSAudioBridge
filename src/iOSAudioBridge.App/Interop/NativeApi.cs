using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace iOSAudioBridge.Interop;
public enum BridgeState : uint { Stopped, Connecting, Ready, Streaming, Stopping, Error }
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode, Pack = 8)]
public struct Phone
{
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string Id;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string Name;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string Model;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string Os;
    public uint Paired, Accessible;
    public readonly string Display => $"{Name}  ·  {Model}  ·  iOS {Os}" + (Accessible == 0 ? "  ·  请解锁并信任电脑" : "");
}
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode, Pack = 8)]
public struct OutputDevice
{
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)] public string Id;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string Name;
    public uint IsDefault, Reserved;
    public readonly string Display => Name + (IsDefault != 0 ? "  ·  系统默认" : "");
}
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode, Pack = 8)]
internal struct Config
{
    public uint Size, Version;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)] public string DeviceId;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)] public string OutputId;
    public uint StartupFrames, HighWaterFrames;
    public float Volume;
    public uint Reserved;
}
[StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode, Pack = 8)]
public struct Status
{
    public uint Size, Version;
    public BridgeState State;
    public int ErrorCode;
    public uint SampleRate, Channels, Bits, OutputActive;
    public uint EnginePeriodFrames, EndpointFrames, QueuedFrames, CapacityFrames;
    public ulong AudioPackets, VideoPackets, VideoBytes, RenderedFrames, DroppedFrames, Underruns, MissingFrames;
    public float InputLeftDb, InputRightDb, OutputLeftDb, OutputRightDb, HostLatencyMs, StreamLatencyMs;
    public uint Autoconvert, SourceIdle;
    [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)] public string Message;
}
public sealed class AudioHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    internal AudioHandle(IntPtr value) : base(true) { SetHandle(value); }
    protected override bool ReleaseHandle() { NativeApi.Destroy(handle); return true; }
}
public static class NativeApi
{
    private const string Library = "AudioCore.dll";
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern int ab_list_devices(IntPtr items, ref uint count);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern int ab_list_outputs(IntPtr items, ref uint count);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern int ab_create(out IntPtr handle);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern int ab_start(AudioHandle handle, in Config config);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern int ab_stop(AudioHandle handle);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern int ab_get_status(AudioHandle handle, ref Status status);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern int ab_set_volume(AudioHandle handle, float volume);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)] private static extern int ab_last_error(StringBuilder text, uint capacity);
    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)] private static extern uint ab_abi_size(uint structure);
    [DllImport(Library, EntryPoint = "ab_destroy", CallingConvention = CallingConvention.Cdecl)] internal static extern void Destroy(IntPtr handle);
    private static void Check(int result)
    {
        if (result == 0) return;
        var message = new StringBuilder(1024); ab_last_error(message, 1024);
        throw new InvalidOperationException(message.Length > 0 ? message.ToString() : $"AudioCore 错误：{result}");
    }
    public static void ValidateAbi()
    {
        Type[] types = [typeof(Phone), typeof(OutputDevice), typeof(Config), typeof(Status)];
        for (var i = 0; i < types.Length; i++) if (Marshal.SizeOf(types[i]) != ab_abi_size((uint)i + 1)) throw new InvalidOperationException("AudioCore 与界面版本不匹配，请重新完整构建。");
    }
    private delegate int Enumerate(IntPtr items, ref uint count);
    private static List<T> List<T>(Enumerate enumerate) where T : struct
    {
        uint count = 0; Check(enumerate(IntPtr.Zero, ref count));
        for (var attempt = 0; attempt < 4; attempt++)
        {
            if (count == 0) return [];
            var size = Marshal.SizeOf<T>();
            var memory = Marshal.AllocHGlobal(checked((int)count * size));
            try
            {
                var result = enumerate(memory, ref count);
                if (result == -2) continue;
                Check(result); var items = new List<T>();
                for (var i = 0; i < count; i++) items.Add(Marshal.PtrToStructure<T>(memory + i * size));
                return items;
            }
            finally { Marshal.FreeHGlobal(memory); }
        }
        throw new InvalidOperationException("设备列表持续变化，请重试刷新。");
    }
    public static List<Phone> Phones() => List<Phone>(ab_list_devices);
    public static List<OutputDevice> Outputs() => List<OutputDevice>(ab_list_outputs);
    public static AudioHandle Create() { Check(ab_create(out var value)); return new AudioHandle(value); }
    public static void Start(AudioHandle handle, string phone, string output, float volume, uint startup = 3072, uint highWater = 4096)
    {
        var config = new Config { Size = (uint)Marshal.SizeOf<Config>(), Version = 1, DeviceId = phone, OutputId = output, Volume = volume, StartupFrames = startup, HighWaterFrames = highWater };
        Check(ab_start(handle, in config));
    }
    public static Status Status(AudioHandle handle)
    {
        var status = new Status { Size = (uint)Marshal.SizeOf<Status>(), Version = 1, Message = "" };
        Check(ab_get_status(handle, ref status)); return status;
    }
    public static void Stop(AudioHandle handle) => Check(ab_stop(handle));
    public static void Volume(AudioHandle handle, float volume) => Check(ab_set_volume(handle, volume));
}
