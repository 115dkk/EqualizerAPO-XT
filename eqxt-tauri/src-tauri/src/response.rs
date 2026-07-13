use crate::config::FilterLine;
use std::f64::consts::{LN_2, PI};

#[derive(Clone, Copy)]
struct Coefficients {
    b0: f64,
    b1: f64,
    b2: f64,
    a0: f64,
    a1: f64,
    a2: f64,
}

fn biquad_coefficients(
    ftype: &str,
    freq_hz: f64,
    gain_db: Option<f64>,
    q: Option<f64>,
    bw_oct: Option<f64>,
    sample_rate: f64,
) -> Option<Coefficients> {
    let w0 = 2.0 * PI * freq_hz / sample_rate;
    let sin_w0 = w0.sin();
    let cos_w0 = w0.cos();

    let alpha = if let Some(q) = q {
        sin_w0 / (2.0 * q)
    } else if let Some(bw_oct) = bw_oct {
        sin_w0 * ((LN_2 / 2.0) * bw_oct * (w0 / sin_w0)).sinh()
    } else {
        sin_w0 / (2.0 * 0.707)
    };

    match ftype {
        "PK" => {
            let a = 10.0_f64.powf(gain_db.unwrap_or(0.0) / 40.0);

            Some(Coefficients {
                b0: 1.0 + alpha * a,
                b1: -2.0 * cos_w0,
                b2: 1.0 - alpha * a,
                a0: 1.0 + alpha / a,
                a1: -2.0 * cos_w0,
                a2: 1.0 - alpha / a,
            })
        }
        "LP" | "LPQ" => Some(Coefficients {
            b0: (1.0 - cos_w0) / 2.0,
            b1: 1.0 - cos_w0,
            b2: (1.0 - cos_w0) / 2.0,
            a0: 1.0 + alpha,
            a1: -2.0 * cos_w0,
            a2: 1.0 - alpha,
        }),
        "HP" | "HPQ" => Some(Coefficients {
            b0: (1.0 + cos_w0) / 2.0,
            b1: -(1.0 + cos_w0),
            b2: (1.0 + cos_w0) / 2.0,
            a0: 1.0 + alpha,
            a1: -2.0 * cos_w0,
            a2: 1.0 - alpha,
        }),
        "BP" => Some(Coefficients {
            b0: alpha,
            b1: 0.0,
            b2: -alpha,
            a0: 1.0 + alpha,
            a1: -2.0 * cos_w0,
            a2: 1.0 - alpha,
        }),
        "NO" | "No" => Some(Coefficients {
            b0: 1.0,
            b1: -2.0 * cos_w0,
            b2: 1.0,
            a0: 1.0 + alpha,
            a1: -2.0 * cos_w0,
            a2: 1.0 - alpha,
        }),
        "AP" => Some(Coefficients {
            b0: 1.0 - alpha,
            b1: -2.0 * cos_w0,
            b2: 1.0 + alpha,
            a0: 1.0 + alpha,
            a1: -2.0 * cos_w0,
            a2: 1.0 - alpha,
        }),
        "LS" | "LSC" => {
            let a = 10.0_f64.powf(gain_db.unwrap_or(0.0) / 40.0);
            let two_sqrt_a_alpha = 2.0 * a.sqrt() * alpha;

            Some(Coefficients {
                b0: a * ((a + 1.0) - (a - 1.0) * cos_w0 + two_sqrt_a_alpha),
                b1: 2.0 * a * ((a - 1.0) - (a + 1.0) * cos_w0),
                b2: a * ((a + 1.0) - (a - 1.0) * cos_w0 - two_sqrt_a_alpha),
                a0: (a + 1.0) + (a - 1.0) * cos_w0 + two_sqrt_a_alpha,
                a1: -2.0 * ((a - 1.0) + (a + 1.0) * cos_w0),
                a2: (a + 1.0) + (a - 1.0) * cos_w0 - two_sqrt_a_alpha,
            })
        }
        "HS" | "HSC" => {
            let a = 10.0_f64.powf(gain_db.unwrap_or(0.0) / 40.0);
            let two_sqrt_a_alpha = 2.0 * a.sqrt() * alpha;

            Some(Coefficients {
                b0: a * ((a + 1.0) + (a - 1.0) * cos_w0 + two_sqrt_a_alpha),
                b1: -2.0 * a * ((a - 1.0) + (a + 1.0) * cos_w0),
                b2: a * ((a + 1.0) + (a - 1.0) * cos_w0 - two_sqrt_a_alpha),
                a0: (a + 1.0) - (a - 1.0) * cos_w0 + two_sqrt_a_alpha,
                a1: 2.0 * ((a - 1.0) - (a + 1.0) * cos_w0),
                a2: (a + 1.0) - (a - 1.0) * cos_w0 - two_sqrt_a_alpha,
            })
        }
        _ => None,
    }
}

fn magnitude_db(coefficients: Coefficients, w: f64) -> f64 {
    let cos_w = w.cos();
    let sin_w = w.sin();
    let cos_2w = (2.0 * w).cos();
    let sin_2w = (2.0 * w).sin();

    let numerator_re = coefficients.b0 + coefficients.b1 * cos_w + coefficients.b2 * cos_2w;
    let numerator_im = -coefficients.b1 * sin_w - coefficients.b2 * sin_2w;

    let denominator_re = coefficients.a0 + coefficients.a1 * cos_w + coefficients.a2 * cos_2w;
    let denominator_im = -coefficients.a1 * sin_w - coefficients.a2 * sin_2w;

    let denominator_magnitude_squared =
        denominator_re * denominator_re + denominator_im * denominator_im;

    let response_re = (numerator_re * denominator_re + numerator_im * denominator_im)
        / denominator_magnitude_squared;
    let response_im = (numerator_im * denominator_re - numerator_re * denominator_im)
        / denominator_magnitude_squared;

    let magnitude = (response_re * response_re + response_im * response_im).sqrt();
    20.0 * magnitude.log10()
}

#[tauri::command]
pub fn frequency_response(lines: Vec<FilterLine>, sample_rate: f64, num_points: usize) -> Vec<[f64; 2]> {
    if num_points < 2 || sample_rate <= 0.0 {
        return Vec::new();
    }

    let mut preamp_db = 0.0;
    let mut biquads = Vec::new();

    for line in lines {
        match line {
            FilterLine::Preamp { gain_db } => {
                preamp_db += gain_db;
            }
            FilterLine::Biquad {
                enabled: true,
                ftype,
                freq_hz,
                gain_db,
                q,
                bw_oct,
                ..
            } => {
                if let Some(coefficients) =
                    biquad_coefficients(&ftype, freq_hz, gain_db, q, bw_oct, sample_rate)
                {
                    biquads.push(coefficients);
                }
            }
            _ => {}
        }
    }

    let min_frequency = 20.0_f64;
    let max_frequency = 20_000.0_f64.min(sample_rate / 2.0);
    let log_min = min_frequency.ln();
    let log_max = max_frequency.ln();
    let denominator = (num_points - 1) as f64;

    let mut response = Vec::with_capacity(num_points);

    for index in 0..num_points {
        let t = index as f64 / denominator;
        let frequency = (log_min + t * (log_max - log_min)).exp();
        let w = 2.0 * PI * frequency / sample_rate;

        let mut total_db = preamp_db;
        for &coefficients in &biquads {
            total_db += magnitude_db(coefficients, w);
        }

        response.push([frequency, total_db]);
    }

    response
}

#[cfg(test)]
mod tests {
    use super::*;

    const SAMPLE_RATE: f64 = 48_000.0;
    const NUM_POINTS: usize = 128;

    fn nearest_db(response: &[[f64; 2]], target_frequency: f64) -> f64 {
        response
            .iter()
            .min_by(|left, right| {
                (left[0] - target_frequency)
                    .abs()
                    .total_cmp(&(right[0] - target_frequency).abs())
            })
            .expect("response must not be empty")[1]
    }

    #[test]
    fn flat_preamp_response() {
        let lines = vec![FilterLine::Preamp { gain_db: -6.0 }];
        let response = frequency_response(lines, SAMPLE_RATE, NUM_POINTS);

        assert_eq!(response.len(), NUM_POINTS);
        for point in response {
            assert!(
                (point[1] - (-6.0)).abs() <= 0.01,
                "frequency {} Hz had response {} dB",
                point[0],
                point[1]
            );
        }
    }

    #[test]
    fn peaking_boost_response() {
        let lines = vec![FilterLine::Biquad {
            enabled: true,
            ftype: "PK".to_string(),
            freq_hz: 1000.0,
            gain_db: Some(6.0),
            q: Some(1.0),
            bw_oct: None,
            index: None,
        }];

        let response = frequency_response(lines, SAMPLE_RATE, NUM_POINTS);
        assert_eq!(response.len(), NUM_POINTS);

        let center_db = nearest_db(&response, 1000.0);
        let low_db = nearest_db(&response, 20.0);
        let high_db = response.last().unwrap()[1];

        assert!(
            (center_db - 6.0).abs() <= 0.6,
            "center response was {center_db} dB"
        );
        assert!(low_db.abs() <= 0.6, "low-frequency response was {low_db} dB");
        assert!(high_db.abs() <= 0.6, "high-frequency response was {high_db} dB");
    }

    #[test]
    fn highpass_attenuates_low_frequencies() {
        let lines = vec![FilterLine::Biquad {
            enabled: true,
            ftype: "HP".to_string(),
            freq_hz: 1000.0,
            gain_db: None,
            q: Some(0.707),
            bw_oct: None,
            index: None,
        }];

        let response = frequency_response(lines, SAMPLE_RATE, NUM_POINTS);
        assert_eq!(response.len(), NUM_POINTS);

        let low_db = response.first().unwrap()[1];
        let high_db = response.last().unwrap()[1];

        assert!(
            high_db - low_db >= 20.0,
            "low response was {low_db} dB and high response was {high_db} dB"
        );
    }
}
