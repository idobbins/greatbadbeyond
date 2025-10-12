use std::collections::VecDeque;

#[derive(Clone, Debug)]
pub struct TimingSummary {
    pub last_total: f32,
    pub sample_count: usize,
    pub avg_total: f32,
    pub p95_total: f32,
    pub p99_total: f32,
}

pub struct TimingStats {
    history: VecDeque<f32>,
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

    pub fn record(&mut self, total_ms: f32) -> Option<TimingSummary> {
        if self.history.len() == self.max_history {
            self.history.pop_front();
        }
        self.history.push_back(total_ms);
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
        let last_total = *self.history.back().unwrap_or(&0.0);
        let mut totals: Vec<f32> = self.history.iter().copied().collect();
        totals.sort_by(|a, b| a.partial_cmp(b).unwrap_or(std::cmp::Ordering::Equal));

        let avg_total = totals.iter().sum::<f32>() / sample_count as f32;
        let p95_total = quantile(&totals, 0.95);
        let p99_total = quantile(&totals, 0.99);

        TimingSummary {
            last_total,
            sample_count,
            avg_total,
            p95_total,
            p99_total,
        }
    }
}

fn quantile(sorted: &[f32], q: f32) -> f32 {
    if sorted.is_empty() {
        return 0.0;
    }
    let clamped_q = q.clamp(0.0, 1.0);
    let idx = ((sorted.len() - 1) as f32 * clamped_q).ceil() as usize;
    *sorted
        .get(idx.min(sorted.len() - 1))
        .unwrap_or(&sorted[sorted.len() - 1])
}
