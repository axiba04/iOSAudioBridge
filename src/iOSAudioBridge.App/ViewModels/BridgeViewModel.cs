using System.ComponentModel;
using System.Runtime.CompilerServices;
using System.Collections.ObjectModel;
using iOSAudioBridge.Interop;

namespace iOSAudioBridge.ViewModels;
public sealed class BridgeViewModel : INotifyPropertyChanged
{
    public ObservableCollection<Phone> Phones { get; } = [];
    public ObservableCollection<OutputDevice> Outputs { get; } = [];
    private string message = "连接 iPhone，解锁并信任此电脑。";
    private string format = "等待音频", counters = "音频包 0   ·   视频包 0", buffer = "0 ms", latency = "—", underruns = "0", dropped = "0", input = "—", output = "—", engine = "等待输出设备";
    private double fill;
    public string Message { get => message; set { message = value; Changed(); } }
    public string Format { get => format; private set { format = value; Changed(); } }
    public string Counters { get => counters; private set { counters = value; Changed(); } }
    public string Buffer { get => buffer; private set { buffer = value; Changed(); } }
    public string Latency { get => latency; private set { latency = value; Changed(); } }
    public string Underruns { get => underruns; private set { underruns = value; Changed(); } }
    public string Dropped { get => dropped; private set { dropped = value; Changed(); } }
    public string Input { get => input; private set { input = value; Changed(); } }
    public string Output { get => output; private set { output = value; Changed(); } }
    public string Engine { get => engine; private set { engine = value; Changed(); } }
    public double Fill { get => fill; private set { fill = value; Changed(); } }
    public void Update(Status status)
    {
        Message = status.State == BridgeState.Streaming && status.SourceIdle != 0
            ? status.QueuedFrames > 0 ? "正在恢复音频播放…" : "已连接，当前没有音频数据"
            : status.Message;
        Format = status.SampleRate == 0 ? "等待音频" : $"{status.SampleRate / 1000.0:0.#} kHz  ·  PCM {status.Bits} bit  ·  Stereo";
        Counters = $"音频包 {status.AudioPackets:N0}   ·   视频包 {status.VideoPackets:N0}";
        Buffer = $"{status.QueuedFrames / 48.0:0.0} ms";
        Fill = status.CapacityFrames == 0 ? 0 : 100.0 * status.QueuedFrames / status.CapacityFrames;
        Latency = $"{status.HostLatencyMs:0.0} ms";
        Underruns = status.Underruns.ToString("N0"); Dropped = status.DroppedFrames.ToString("N0");
        Input = $"L {status.InputLeftDb:0.0}  /  R {status.InputRightDb:0.0} dBFS";
        Output = $"L {status.OutputLeftDb:0.0}  /  R {status.OutputRightDb:0.0} dBFS";
        Engine = status.EndpointFrames == 0 ? "等待输出设备" : $"共享模式 · 周期 {status.EnginePeriodFrames / 48.0:0.0} ms · 端点 {status.EndpointFrames / 48.0:0.0} ms" + (status.Autoconvert != 0 ? " · 系统格式转换" : "");
    }
    public event PropertyChangedEventHandler? PropertyChanged;
    private void Changed([CallerMemberName] string? name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
}
