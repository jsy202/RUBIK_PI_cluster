% Analyze Qt digital-cluster E2E latency logs.
% Usage:
%   analyze_latency("latency_metrics_YYYYMMDD_hhmmss.csv")

function analyze_latency(csv_file)
    if nargin < 1
        error("CSV file path is required.");
    end

    T = readtable(csv_file);
    latency = T.e2e_latency_ms;

    mean_ms = mean(latency);
    jitter_ms = std(latency, 1);
    worst_ms = max(latency);

    fprintf("samples: %d\n", height(T));
    fprintf("mean: %.2f ms\n", mean_ms);
    fprintf("jitter/stddev: %.2f ms\n", jitter_ms);
    fprintf("worst: %.2f ms\n", worst_ms);

    figure;
    plot(T.seq, latency);
    hold on;
    yline(mean_ms);
    grid on;
    xlabel("Sample");
    ylabel("Latency (ms)");
    title("E2E Latency Trend");
    legend("latency", "mean");
    saveas(gcf, "latency_trend.png");

    figure;
    histogram(latency, 40, "Normalization", "pdf");
    grid on;
    xlabel("Latency (ms)");
    ylabel("Probability Density");
    title("E2E Latency Distribution");
    saveas(gcf, "latency_distribution.png");
end
