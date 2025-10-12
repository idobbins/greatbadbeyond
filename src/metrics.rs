use std::collections::VecDeque;

#[derive(Clone, Copy, Debug)]
pub struct FrameDurations {
    pub render_ms: f32,
    pub denoise_ms: f32,
    pub total_ms: f32,
}

impl FrameDurations {
    pub fn new(render_ms: f32, denoise_ms: f32, total_ms: f32) -> Self {
        Self {
            render_ms,
            denoise_ms,
            total_ms,
        }
    }
}

#[derive(Clone, Debug)]
pub struct TimingSummary {
    pub last: FrameDurations,
    pub sample_count: usize,
    pub avg_render: f32,
    pub avg_denoise: f32,
    pub avg_total: f32,
    pub p95_total: f32,
}

pub struct TimingStats {
    history: VecDeque<FrameDurations>,
    max_history: usize,
    sample_interval: u32,
    frames_since_update: u32,
    last_summary: Option<TimingSummary>,
}

impl TimingStats {
    pub fn new(sample_interval: u32, max_history: usize) -> Self {
        Self {
            history: VecDeque::with_capacity(max_history),
            max_history,
            sample_interval: sample_interval.max(1),
            frames_since_update: 0,
            last_summary: None,
        }
    }

    pub fn record(&mut self, durations: FrameDurations) -> Option<TimingSummary> {
        if self.history.len() == self.max_history {
            self.history.pop_front();
        }
        self.history.push_back(durations);
        self.frames_since_update += 1;

        let needs_update =
            self.frames_since_update >= self.sample_interval || self.last_summary.is_none();
        if !needs_update {
            return None;
        }

        self.frames_since_update = 0;
        let summary = self.summarize();
        self.last_summary = Some(summary.clone());
        Some(summary)
    }

    fn summarize(&self) -> TimingSummary {
        let sample_count = self.history.len().max(1);

        let mut sum_render = 0.0f32;
        let mut sum_denoise = 0.0f32;
        let mut sum_total = 0.0f32;
        let mut totals = Vec::with_capacity(sample_count);

        for frame in &self.history {
            sum_render += frame.render_ms;
            sum_denoise += frame.denoise_ms;
            sum_total += frame.total_ms;
            totals.push(frame.total_ms);
        }

        totals.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));
        let p95_index = (((totals.len() - 1) as f32) * 0.95).ceil() as usize;
        let p95_index = p95_index.min(totals.len() - 1);
        let p95_total = totals[p95_index];

        TimingSummary {
            last: *self.history.back().unwrap(),
            sample_count,
            avg_render: sum_render / sample_count as f32,
            avg_denoise: sum_denoise / sample_count as f32,
            avg_total: sum_total / sample_count as f32,
            p95_total,
        }
    }
}
