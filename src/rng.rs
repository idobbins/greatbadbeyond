pub struct Pcg32 {
    state: u32,
}

impl Pcg32 {
    pub fn new(seed: u32) -> Self {
        Self { state: seed }
    }

    pub fn next_u32(&mut self) -> u32 {
        let mut v = self.state;
        v = v.wrapping_mul(747_796_405).wrapping_add(2_891_336_453);
        let word = ((v >> ((v >> 28) + 4)) ^ v).wrapping_mul(277_803_737);
        let result = (word >> 22) ^ word;
        self.state = result;
        result
    }

    pub fn next_f32(&mut self) -> f32 {
        const SCALE: f32 = 1.0 / 4_294_967_296.0;
        self.next_u32() as f32 * SCALE
    }
}
