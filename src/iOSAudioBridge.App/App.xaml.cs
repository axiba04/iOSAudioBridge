using System.IO;
using System.Text.Json;
using System.Windows;
using iOSAudioBridge.Interop;

namespace iOSAudioBridge;
public partial class App : Application
{
    public App()
    {
        DispatcherUnhandledException += (_, args) =>
        {
            try { File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "ui-unhandled-error.txt"), args.Exception.ToString()); } catch { }
            args.Handled = true;
            // Let the normal asynchronous close path stop capture and restore USB.
            if (MainWindow is MainWindow window && window.IsLoaded) window.Close();
            else Shutdown(1);
        };
    }
    protected override async void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        var logFolder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "iOSAudioBridge", "logs");
        try
        {
            Directory.CreateDirectory(logFolder);
            Environment.SetEnvironmentVariable("IPHONE_MIRROR_LOG_FILE", Path.Combine(logFolder, "audio-core.log"));
        }
        catch (IOException) { /* Native logging falls back to TEMP. */ }
        catch (UnauthorizedAccessException) { /* Diagnostics must not prevent startup. */ }
        if (e.Args.Contains("--ui-smoke"))
        {
            ShutdownMode = ShutdownMode.OnExplicitShutdown;
            try
            {
                NativeApi.ValidateAbi();
                var result = await new MainWindow().SmokeTest();
                File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "ui-smoke.json"), JsonSerializer.Serialize(result));
                Shutdown(0);
            }
            catch (Exception error)
            {
                File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "ui-smoke.json"), JsonSerializer.Serialize(new { passed = false, error = error.ToString() }));
                Shutdown(1);
            }
            return;
        }
        if (e.Args.Contains("--interop-smoke"))
        {
            ShutdownMode = ShutdownMode.OnExplicitShutdown;
            try
            {
                var result = await Task.Run(() => {
                    NativeApi.ValidateAbi();
                    using var handle = NativeApi.Create();
                    var status = NativeApi.Status(handle);
                    if (status.State != BridgeState.Stopped) throw new InvalidOperationException("Unexpected initial state");
                    NativeApi.Volume(handle, .5f);
                    NativeApi.Stop(handle);
                    return new { passed = true, framework = Environment.Version.ToString(), outputs = NativeApi.Outputs().Count, state = status.State.ToString() };
                });
                File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "interop-smoke.json"), JsonSerializer.Serialize(result));
                Shutdown(0);
            }
            catch (Exception error)
            {
                File.WriteAllText(Path.Combine(AppContext.BaseDirectory, "interop-smoke.json"), JsonSerializer.Serialize(new { passed = false, error = error.ToString() }));
                Shutdown(1);
            }
            return;
        }
        try { NativeApi.ValidateAbi(); MainWindow = new MainWindow(); MainWindow.Show(); }
        catch (Exception error) { MessageBox.Show(error.Message, "无法启动 iOS Audio Bridge", MessageBoxButton.OK, MessageBoxImage.Error); Shutdown(1); }
    }
}
