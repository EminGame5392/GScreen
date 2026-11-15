using System.Text.Json.Nodes;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Media;
using Windows.Graphics;
using Windows.UI;

namespace GScreen.App;

public sealed partial class MainWindow : Window
{
    private record Preset(string Id, string Name, double[] Ratios);

    // Same presets and zone math as the core (core/src/Layout.cpp).
    private static readonly Preset[] Presets =
    [
        new("three-equal", "Три равные", [1, 1, 1]),
        new("focus-center", "Фокус в центре", [1, 2, 1]),
        new("two-halves", "Две половины", [1, 1]),
        new("game-chat", "Игра + чат", [7, 3]),
    ];
    private const int MinZoneWidth = 640, WidthAlign = 8, DividerPx = 6;
    private static readonly int[] RefreshOptions = [240, 165, 144, 120, 60];
    private static readonly string[] ZoneNames = ["A", "B", "C"];

    private readonly CoreClient _core = new();
    private readonly DispatcherTimer _timer = new() { Interval = TimeSpan.FromSeconds(1) };
    private double[] _ratios = [1, 1, 1];
    private string _presetId = "three-equal";
    private int _panelW = 5120, _panelH = 1440;
    private double _panelHz = 240;
    private string _mode = "merged";
    private bool _busy, _coreStartAttempted;

    public MainWindow()
    {
        InitializeComponent();
        ExtendsContentIntoTitleBar = true;
        SetTitleBar(TitleBar);
        AppWindow.Resize(new SizeInt32(1180, 760));

        BuildPresetButtons();
        RebuildZones();
        UpdateModeButtons();
        _timer.Tick += async (_, _) => await RefreshStatusAsync();
        _timer.Start();
        _ = RefreshStatusAsync();
    }

    // ---------- zone math ----------

    private static int[] ComputeWidths(int panelW, double[] ratiosIn)
    {
        var ratios = ratiosIn.Where(r => r > 0 && double.IsFinite(r)).Take(3).ToList();
        while (ratios.Count > 1 && ratios.Count * MinZoneWidth > panelW) ratios.RemoveAt(ratios.Count - 1);
        if (ratios.Count == 0) return [panelW];
        double sum = ratios.Sum();
        var w = new int[ratios.Count];
        int used = 0;
        for (int i = 0; i + 1 < ratios.Count; i++)
        {
            int ideal = (int)Math.Round(panelW * ratios[i] / sum / WidthAlign, MidpointRounding.AwayFromZero) * WidthAlign;
            int maxHere = panelW - used - (ratios.Count - 1 - i) * MinZoneWidth;
            w[i] = Math.Clamp(ideal, MinZoneWidth, Math.Max(MinZoneWidth, maxHere / WidthAlign * WidthAlign));
            used += w[i];
        }
        w[^1] = panelW - used;
        return w;
    }

    private Color ZoneColor(int i) =>
        (Color)Application.Current.Resources[i switch { 0 => "ZoneAColor", 1 => "ZoneBColor", _ => "ZoneCColor" }];

    // ---------- UI building ----------

    private void BuildPresetButtons()
    {
        PresetList.Children.Clear();
        foreach (var p in Presets)
        {
            var mini = new Grid { Width = 64, Height = 12, ColumnSpacing = 2 };
            for (int i = 0; i < p.Ratios.Length; i++)
            {
                mini.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(p.Ratios[i], GridUnitType.Star) });
                var bar = new Border { CornerRadius = new CornerRadius(2), Background = new SolidColorBrush(ZoneColor(i)), Opacity = 0.7 };
                Grid.SetColumn(bar, i);
                mini.Children.Add(bar);
            }
            var content = new Grid { HorizontalAlignment = HorizontalAlignment.Stretch };
            content.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            content.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            content.Children.Add(new TextBlock { Text = p.Name, VerticalAlignment = VerticalAlignment.Center });
            Grid.SetColumn(mini, 1);
            content.Children.Add(mini);

            var b = new ToggleButton
            {
                Content = content,
                Tag = p.Id,
                HorizontalAlignment = HorizontalAlignment.Stretch,
                HorizontalContentAlignment = HorizontalAlignment.Stretch,
                Padding = new Thickness(10, 8, 10, 8),
            };
            b.Click += (_, _) =>
            {
                _ratios = (double[])p.Ratios.Clone();
                _presetId = p.Id;
                RebuildZones();
            };
            PresetList.Children.Add(b);
        }
        UpdatePresetButtons();
    }

    private void UpdatePresetButtons()
    {
        foreach (var b in PresetList.Children.OfType<ToggleButton>())
            b.IsChecked = (string)b.Tag == _presetId;
    }

    private void RebuildZones()
    {
        // The preview is always the layout editor; the mode toggle and status line show what is live.
        const bool merged = false;
        int[] widths = ComputeWidths(_panelW, _ratios);
        ZoneHost.Children.Clear();
        ZoneHost.ColumnDefinitions.Clear();
        ZoneCards.Children.Clear();
        ZoneCards.ColumnDefinitions.Clear();

        for (int i = 0; i < widths.Length; i++)
        {
            if (i > 0)
            {
                ZoneHost.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(DividerPx) });
                var thumb = new Thumb { Tag = i, Background = new SolidColorBrush(Color.FromArgb(0x80, 0xE8, 0xEC, 0xF5)) };
                thumb.DragDelta += OnDividerDrag;
                thumb.DragCompleted += (_, _) => RebuildZones();
                ToolTipService.SetToolTip(thumb, "Потяните, чтобы изменить ширину зон");
                Grid.SetColumn(thumb, ZoneHost.ColumnDefinitions.Count - 1);
                ZoneHost.Children.Add(thumb);
            }
            ZoneHost.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(widths[i], GridUnitType.Star) });

            var color = merged ? Colors.SteelBlue : ZoneColor(i);
            var zone = new Grid
            {
                Background = new SolidColorBrush(Color.FromArgb(0x38, color.R, color.G, color.B)),
                Padding = new Thickness(10, 8, 10, 8),
            };
            var label = new StackPanel { VerticalAlignment = VerticalAlignment.Bottom };
            label.Children.Add(new TextBlock { Text = merged ? "G9" : ZoneNames[i], FontSize = 26, FontWeight = Microsoft.UI.Text.FontWeights.Bold, Foreground = new SolidColorBrush(Colors.White) });
            label.Children.Add(new TextBlock
            {
                Text = $"{widths[i]}×{_panelH} · {SelectedRefresh()} Гц",
                FontFamily = (FontFamily)Application.Current.Resources["MonoFont"],
                FontSize = 12,
                Foreground = new SolidColorBrush(Color.FromArgb(0xD0, 0xFF, 0xFF, 0xFF)),
            });
            zone.Children.Add(label);
            Grid.SetColumn(zone, ZoneHost.ColumnDefinitions.Count - 1);
            ZoneHost.Children.Add(zone);

            // Card under the preview.
            ZoneCards.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
            var card = new Border
            {
                BorderBrush = new SolidColorBrush(color),
                BorderThickness = new Thickness(0, 3, 0, 0),
                Background = (Brush)Application.Current.Resources["CardBackgroundFillColorDefaultBrush"],
                CornerRadius = new CornerRadius(8),
                Padding = new Thickness(12, 10, 12, 10),
            };
            var cardText = new StackPanel { Spacing = 2 };
            cardText.Children.Add(new TextBlock { Text = merged ? "Единый экран" : $"Монитор {ZoneNames[i]}", FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
            cardText.Children.Add(new TextBlock { Text = $"Разрешение  {widths[i]}×{_panelH}", FontFamily = (FontFamily)Application.Current.Resources["MonoFont"], FontSize = 12 });
            cardText.Children.Add(new TextBlock { Text = merged ? $"Частота  {_panelHz:0} Гц · VRR" : $"Частота  {SelectedRefresh()} Гц", FontFamily = (FontFamily)Application.Current.Resources["MonoFont"], FontSize = 12 });
            card.Child = cardText;
            Grid.SetColumn(card, i);
            ZoneCards.Children.Add(card);
        }
        UpdatePresetButtons();
    }

    private void OnDividerDrag(object sender, DragDeltaEventArgs e)
    {
        var thumb = (Thumb)sender;
        int i = (int)thumb.Tag;
        double zonesWidth = ZoneHost.ActualWidth - DividerPx * (_ratios.Length - 1);
        if (zonesWidth <= 0) return;
        double sum = _ratios.Sum();
        var fr = _ratios.Select(r => r / sum).ToArray();
        double minFr = (double)MinZoneWidth / _panelW;
        double pair = fr[i - 1] + fr[i];
        double left = Math.Clamp(fr[i - 1] + e.HorizontalChange / zonesWidth, minFr, pair - minFr);
        fr[i - 1] = left;
        fr[i] = pair - left;
        _ratios = fr;
        _presetId = "custom";
        // Live resize without rebuilding (keeps the drag going): update star widths in place.
        int z = 0;
        foreach (var col in ZoneHost.ColumnDefinitions)
            if (col.Width.GridUnitType == GridUnitType.Star) col.Width = new GridLength(fr[z++], GridUnitType.Star);
        UpdatePresetButtons();
    }

    private void OnPanelSizeChanged(object sender, SizeChangedEventArgs e)
    {
        // Keep the preview at the panel's own aspect ratio (32:9 on the Odyssey G9).
        double h = e.NewSize.Width * _panelH / _panelW;
        if (Math.Abs(PanelFrame.Height - h) > 1) PanelFrame.Height = h;
    }

    private int SelectedRefresh() => RefreshOptions[Math.Max(0, RefreshBox.SelectedIndex)];

    private void UpdateModeButtons()
    {
        SplitMode.IsChecked = _mode == "split";
        MergedMode.IsChecked = _mode != "split";
    }

    // ---------- actions ----------

    private async void OnApplyClick(object sender, RoutedEventArgs e) => await RunAsync(() => _core.ApplyAsync(_ratios, _presetId, SelectedRefresh(), split: true));

    private async void OnSplitModeClick(object sender, RoutedEventArgs e)
    {
        UpdateModeButtons(); // the core decides; reflect real state until it answers
        await RunAsync(() => _core.ApplyAsync(_ratios, _presetId, SelectedRefresh(), split: true));
    }

    private async void OnMergedModeClick(object sender, RoutedEventArgs e)
    {
        UpdateModeButtons();
        await RunAsync(_core.MergeAsync);
    }

    private async Task RunAsync(Func<Task<JsonObject?>> action)
    {
        if (_busy) return;
        _busy = true;
        Busy.IsActive = true;
        ApplyButton.IsEnabled = SplitMode.IsEnabled = MergedMode.IsEnabled = false;
        try
        {
            var r = await action();
            if (r is null) ShowBanner(InfoBarSeverity.Error, "Нет связи с GScreen.Core", "Фоновая служба не отвечает. Проверьте, что она запущена (значок в трее).");
            else if (r["ok"]?.GetValue<bool>() != true) ShowBanner(InfoBarSeverity.Warning, "Не получилось", r["error"]?.GetValue<string>() ?? "");
            else Banner.IsOpen = false;
            if (r?["status"] is JsonObject s) ApplyStatus(s);
        }
        finally
        {
            _busy = false;
            Busy.IsActive = false;
            ApplyButton.IsEnabled = SplitMode.IsEnabled = MergedMode.IsEnabled = true;
        }
    }

    private void ShowBanner(InfoBarSeverity sev, string title, string message)
    {
        Banner.Severity = sev;
        Banner.Title = title;
        Banner.Message = message;
        Banner.IsOpen = true;
    }

    // ---------- status ----------

    private async Task RefreshStatusAsync()
    {
        if (_busy) return;
        var r = await _core.StatusAsync();
        if (r is null)
        {
            if (!_coreStartAttempted)
            {
                _coreStartAttempted = true;
                if (!CoreClient.TryStartCore())
                    ShowBanner(InfoBarSeverity.Error, "Не удалось запустить GScreen.Core",
                        "Если включён Smart App Control, Windows блокирует неподписанные программы. Подробности в README.");
            }
            StatusLine.Text = "● нет связи с ядром";
            return;
        }
        if (r["status"] is JsonObject s) ApplyStatus(s);
    }

    private void ApplyStatus(JsonObject s)
    {
        if (s["panel"] is JsonObject p)
        {
            _panelW = p["width"]?.GetValue<int>() is int w and > 0 ? w : _panelW;
            _panelH = p["height"]?.GetValue<int>() is int h and > 0 ? h : _panelH;
            _panelHz = p["refreshHz"]?.GetValue<double>() ?? _panelHz;
        }
        string mode = s["mode"]?.GetValue<string>() ?? "merged";
        bool modeChanged = mode != _mode;
        _mode = mode;
        UpdateModeButtons();
        if (modeChanged) RebuildZones();

        bool vdd = s["vddInstalled"]?.GetValue<bool>() ?? false;
        string presentation = s["presentation"]?.GetValue<string>() ?? "";
        string err = s["error"]?.GetValue<string>() ?? "";
        if (!vdd && !Banner.IsOpen)
            ShowBanner(InfoBarSeverity.Informational, "Нужен драйвер виртуальных мониторов",
                "Установите Virtual Display Driver: winget install --id=VirtualDrivers.Virtual-Display-Driver -e");
        else if (!string.IsNullOrEmpty(err) && !Banner.IsOpen)
            ShowBanner(InfoBarSeverity.Warning, "Последняя ошибка", err);

        string where = presentation switch
        {
            "exclusive" => "монопольный вывод",
            "window" => "оконный вывод",
            "preview" => "предпросмотр",
            _ => presentation,
        };
        if (mode == "split")
        {
            double fps = s["fps"]?.GetValue<double>() ?? 0;
            double ms = s["frameMs"]?.GetValue<double>() ?? 0;
            StatusLine.Text = $"● компоновщик работает · {where} · {fps:0} кадр/с · сборка кадра {ms:0.00} мс";
        }
        else
        {
            StatusLine.Text = $"● прямой вывод · {(s["panel"]?["name"]?.GetValue<string>() ?? "монитор")} {_panelW}×{_panelH} @ {_panelHz:0} Гц · " +
                              $"режим зон: {where}";
        }
    }
}
