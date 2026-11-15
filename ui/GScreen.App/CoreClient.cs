using System.Diagnostics;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace GScreen.App;

/// <summary>Talks to GScreen.Core over \\.\pipe\GScreen.Core (one JSON object per line).</summary>
public sealed class CoreClient
{
    private const string PipeName = "GScreen.Core";
    private readonly SemaphoreSlim _lock = new(1, 1);

    public async Task<JsonObject?> SendAsync(JsonObject request, TimeSpan timeout)
    {
        await _lock.WaitAsync();
        try
        {
            using var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut, PipeOptions.Asynchronous);
            using var cts = new CancellationTokenSource(timeout);
            await pipe.ConnectAsync(1500, cts.Token);
            var line = Encoding.UTF8.GetBytes(request.ToJsonString() + "\n");
            await pipe.WriteAsync(line, cts.Token);
            await pipe.FlushAsync(cts.Token);

            var buffer = new List<byte>();
            var chunk = new byte[8192];
            while (true)
            {
                int n = await pipe.ReadAsync(chunk, cts.Token);
                if (n == 0) break;
                int nl = Array.IndexOf(chunk, (byte)'\n', 0, n);
                buffer.AddRange(nl >= 0 ? chunk.Take(nl) : chunk.Take(n));
                if (nl >= 0) break;
            }
            return JsonNode.Parse(Encoding.UTF8.GetString(buffer.ToArray())) as JsonObject;
        }
        catch (Exception ex) when (ex is TimeoutException or OperationCanceledException or IOException or JsonException)
        {
            return null;
        }
        finally
        {
            _lock.Release();
        }
    }

    public Task<JsonObject?> StatusAsync() => SendAsync(new JsonObject { ["cmd"] = "status" }, TimeSpan.FromSeconds(3));

    // Splitting re-plugs virtual monitors and can take several seconds.
    public Task<JsonObject?> ApplyAsync(double[] ratios, string presetId, int refreshHz, bool split) =>
        SendAsync(new JsonObject
        {
            ["cmd"] = "apply",
            ["layout"] = new JsonObject
            {
                ["mode"] = split ? "split" : "merged",
                ["ratios"] = new JsonArray(ratios.Select(r => (JsonNode)JsonValue.Create(r)!).ToArray()),
                ["presetId"] = presetId,
                ["refreshHz"] = refreshHz,
            },
        }, TimeSpan.FromSeconds(60));

    public Task<JsonObject?> MergeAsync() => SendAsync(new JsonObject { ["cmd"] = "merge" }, TimeSpan.FromSeconds(30));

    /// <summary>Starts GScreen.Core.exe from the app folder if it is not running.</summary>
    public static bool TryStartCore()
    {
        var path = Path.Combine(AppContext.BaseDirectory, "GScreen.Core.exe");
        if (!File.Exists(path)) return false;
        try
        {
            Process.Start(new ProcessStartInfo(path) { UseShellExecute = true, WorkingDirectory = AppContext.BaseDirectory });
            return true;
        }
        catch (System.ComponentModel.Win32Exception)
        {
            return false; // blocked (e.g. Smart App Control) or missing
        }
    }
}
