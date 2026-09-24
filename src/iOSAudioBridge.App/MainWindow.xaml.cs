using System.ComponentModel;
using System.IO;
using System.Text.Json;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using iOSAudioBridge.Interop;
using iOSAudioBridge.ViewModels;

namespace iOSAudioBridge;
public partial class MainWindow : Window
{
    private readonly BridgeViewModel model = new();
    private readonly AudioHandle handle = NativeApi.Create();
    private readonly DispatcherTimer timer = new() { Interval = TimeSpan.FromMilliseconds(100) };
    private bool busy, closing, closeAllowed;
    private TaskCompletionSource? operation;
    private BridgeState state;
    private string savedOutput = "";
    private static string SettingsPath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "iOSAudioBridge", "settings.json");
    public MainWindow()
    {
        InitializeComponent(); DataContext = model;
        try { if (File.Exists(SettingsPath)) savedOutput = JsonSerializer.Deserialize<Settings>(File.ReadAllText(SettingsPath))?.OutputId ?? ""; } catch { }
        timer.Tick += (_, _) => Poll();
    }
    private async void OnLoaded(object sender, RoutedEventArgs e) { await Refresh(); if (!closing) timer.Start(); }
    // Constructs XAML and exercises bindings without showing a window or starting USB capture.
    internal async Task<object> SmokeTest()
    {
        try
        {
            await Refresh();
            await Dispatcher.InvokeAsync(() => { }, DispatcherPriority.ContextIdle);
            if (model.Outputs.Count == 0 || OutputSelect.SelectedItem is not OutputDevice)
                throw new InvalidOperationException("Output binding/selection failed");
            if (model.Phones.Count > 0 && !StartButton.IsEnabled)
                throw new InvalidOperationException("Start button state failed");
            MuteBox.IsChecked = true; VolumeSlider.Value = 25; MuteBox.IsChecked = false;
            return new { passed = true, phones = model.Phones.Count, outputs = model.Outputs.Count, xaml = "loaded", controls = "ready" };
        }
        finally { timer.Stop(); await Task.Run(() => handle.Dispose()); closeAllowed = true; Close(); }
    }
    private void BeginOperation() { operation = new(TaskCreationOptions.RunContinuationsAsynchronously); busy = true; Controls(); }
    private void EndOperation() { busy = false; operation?.TrySetResult(); Controls(); }
    private async Task Refresh()
    {
        if (busy || closing || IsRunning) return;
        BeginOperation();
        var priorPhone = (PhoneSelect.SelectedItem as Phone?)?.Id;
        try
        {
            var values = await Task.Run(() => (Phones: NativeApi.Phones(), Outputs: NativeApi.Outputs()));
            model.Phones.Clear(); foreach (var phone in values.Phones) model.Phones.Add(phone);
            model.Outputs.Clear(); foreach (var output in values.Outputs) model.Outputs.Add(output);
            PhoneSelect.SelectedIndex = Math.Max(0, values.Phones.FindIndex(p => p.Id == priorPhone));
            var outputIndex = values.Outputs.FindIndex(o => o.Id == savedOutput);
            if (outputIndex < 0) outputIndex = values.Outputs.FindIndex(o => o.IsDefault != 0);
            OutputSelect.SelectedIndex = outputIndex < 0 && values.Outputs.Count > 0 ? 0 : outputIndex;
            model.Message = values.Phones.Count == 0 ? "未检测到 USB iPhone。请连接、解锁并信任此电脑，然后刷新。" : "选择耳机 / DAC 后开始监听。";
        }
        catch (Exception error) { model.Message = error.Message; }
        finally { EndOperation(); }
    }
    private bool IsRunning => state is BridgeState.Connecting or BridgeState.Ready or BridgeState.Streaming or BridgeState.Stopping;
    private void Controls()
    {
        if (StartButton is null) return;
        StartButton.IsEnabled = !busy && !closing && !IsRunning && PhoneSelect.SelectedItem is Phone && OutputSelect.SelectedItem is OutputDevice;
        StopButton.IsEnabled = !busy && !closing && IsRunning;
        RefreshButton.IsEnabled = !busy && !closing && !IsRunning;
        PhoneSelect.IsEnabled = OutputSelect.IsEnabled = !busy && !closing && !IsRunning;
    }
    private void Poll()
    {
        if (closing || handle.IsClosed || (busy && !IsRunning)) return;
        try { var status = NativeApi.Status(handle); state = status.State; if (IsRunning || state == BridgeState.Error) model.Update(status); Controls(); }
        catch (Exception error) { model.Message = error.Message; timer.Stop(); }
    }
    private async void RefreshClicked(object sender, RoutedEventArgs e) => await Refresh();
    private async void StartClicked(object sender, RoutedEventArgs e)
    {
        if (busy || closing || PhoneSelect.SelectedItem is not Phone phone || OutputSelect.SelectedItem is not OutputDevice output) return;
        BeginOperation();
        var volume = MuteBox.IsChecked == true ? 0 : (float)(VolumeSlider.Value / 100);
        try { await Task.Run(() => NativeApi.Start(handle, phone.Id, output.Id, volume)); state = BridgeState.Connecting; model.Message = "正在连接…"; }
        catch (Exception error) { model.Message = error.Message; }
        finally { EndOperation(); }
    }
    private async void StopClicked(object sender, RoutedEventArgs e)
    {
        if (busy || closing) return;
        BeginOperation(); model.Message = "正在停止并恢复 USB，请稍候…";
        try { await Task.Run(() => NativeApi.Stop(handle)); model.Message = "已停止，USB 已释放。"; }
        catch (Exception error) { model.Message = error.Message; }
        finally
        {
            try { var status = NativeApi.Status(handle); state = status.State; model.Update(status); }
            catch (Exception error) { model.Message = error.Message; }
            EndOperation();
        }
    }
    private void SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        Controls();
        if (busy || OutputSelect?.SelectedItem is not OutputDevice output) return;
        savedOutput = output.Id;
        try { Directory.CreateDirectory(Path.GetDirectoryName(SettingsPath)!); File.WriteAllText(SettingsPath, JsonSerializer.Serialize(new Settings(savedOutput))); } catch { }
    }
    private void ApplyVolume()
    {
        if (VolumeSlider is null || MuteBox is null || closing || handle.IsClosed) return;
        try { NativeApi.Volume(handle, MuteBox.IsChecked == true ? 0 : (float)(VolumeSlider.Value / 100)); } catch (Exception error) { model.Message = error.Message; }
    }
    private void VolumeChanged(object sender, RoutedPropertyChangedEventArgs<double> e) => ApplyVolume();
    private void MuteChanged(object sender, RoutedEventArgs e) => ApplyVolume();
    private async void OnClosing(object? sender, CancelEventArgs e)
    {
        if (closeAllowed) return;
        e.Cancel = true;
        if (closing) return;
        closing = true; timer.Stop(); Controls(); model.Message = "正在释放音频并恢复 USB，完成后窗口会自动关闭…";
        try
        {
            if (busy && operation is not null) await operation.Task;
            await Task.Run(() => { try { NativeApi.Stop(handle); } finally { handle.Dispose(); } });
        }
        catch (Exception error) { MessageBox.Show(this, error.Message, "音频会话结束", MessageBoxButton.OK, MessageBoxImage.Information); }
        closeAllowed = true; Close();
    }
    private sealed record Settings(string OutputId);
}
