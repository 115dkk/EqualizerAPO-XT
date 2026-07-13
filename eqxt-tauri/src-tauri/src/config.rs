use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
#[serde(tag = "kind")]
pub enum FilterLine {
    Preamp {
        gain_db: f64,
    },
    Biquad {
        enabled: bool,
        ftype: String,
        freq_hz: f64,
        gain_db: Option<f64>,
        q: Option<f64>,
        bw_oct: Option<f64>,
        index: Option<u32>,
    },
    GraphicEq {
        points: Vec<[f64; 2]>,
    },
    Comment {
        text: String,
    },
    Blank,
    Raw {
        text: String,
    },
}

pub fn parse_config(text: &str) -> Vec<FilterLine> {
    text.lines().map(parse_line).collect()
}

pub fn serialize_config(lines: &[FilterLine]) -> String {
    lines
        .iter()
        .map(serialize_line)
        .collect::<Vec<_>>()
        .join("\n")
}

#[tauri::command]
pub fn load_config_text(text: String) -> Vec<FilterLine> {
    parse_config(&text)
}

#[tauri::command]
pub fn save_config_text(lines: Vec<FilterLine>) -> String {
    serialize_config(&lines)
}

fn parse_line(line: &str) -> FilterLine {
    let trimmed = line.trim();

    if trimmed.is_empty() {
        return FilterLine::Blank;
    }

    if let Some(rest) = trimmed.strip_prefix('#') {
        let text = rest.strip_prefix(' ').unwrap_or(rest).to_owned();
        return FilterLine::Comment { text };
    }

    if let Some(parsed) = parse_preamp(trimmed) {
        return parsed;
    }

    if let Some(parsed) = parse_filter(trimmed) {
        return parsed;
    }

    if let Some(parsed) = parse_graphic_eq(trimmed) {
        return parsed;
    }

    FilterLine::Raw {
        text: line.to_owned(),
    }
}

fn parse_preamp(line: &str) -> Option<FilterLine> {
    let (head, body) = line.split_once(':')?;

    if !head.trim().eq_ignore_ascii_case("Preamp") {
        return None;
    }

    let tokens: Vec<&str> = body.split_whitespace().collect();

    if tokens.len() != 2 || !tokens[1].eq_ignore_ascii_case("dB") {
        return None;
    }

    let gain_db = tokens[0].parse::<f64>().ok()?;

    Some(FilterLine::Preamp { gain_db })
}

fn parse_filter(line: &str) -> Option<FilterLine> {
    let (header, body) = line.split_once(':')?;
    let header_tokens: Vec<&str> = header.split_whitespace().collect();

    if header_tokens.is_empty()
        || !header_tokens[0].eq_ignore_ascii_case("Filter")
        || header_tokens.len() > 2
    {
        return None;
    }

    let index = if header_tokens.len() == 2 {
        Some(header_tokens[1].parse::<u32>().ok()?)
    } else {
        None
    };

    let tokens: Vec<&str> = body.split_whitespace().collect();

    if tokens.len() < 5 {
        return None;
    }

    let enabled = if tokens[0].eq_ignore_ascii_case("ON") {
        true
    } else if tokens[0].eq_ignore_ascii_case("OFF") {
        false
    } else {
        return None;
    };

    let ftype = tokens[1].to_ascii_uppercase();

    if !is_supported_filter_type(&ftype)
        || !tokens[2].eq_ignore_ascii_case("Fc")
        || !tokens[4].eq_ignore_ascii_case("Hz")
    {
        return None;
    }

    let freq_hz = tokens[3].parse::<f64>().ok()?;

    let mut gain_db = None;
    let mut q = None;
    let mut bw_oct = None;
    let mut position = 5;

    while position < tokens.len() {
        if tokens[position].eq_ignore_ascii_case("Gain") {
            if gain_db.is_some()
                || position + 2 >= tokens.len()
                || !tokens[position + 2].eq_ignore_ascii_case("dB")
            {
                return None;
            }

            gain_db = Some(tokens[position + 1].parse::<f64>().ok()?);
            position += 3;
        } else if tokens[position].eq_ignore_ascii_case("Q") {
            if q.is_some() || position + 1 >= tokens.len() {
                return None;
            }

            q = Some(tokens[position + 1].parse::<f64>().ok()?);
            position += 2;
        } else if tokens[position].eq_ignore_ascii_case("BW") {
            if bw_oct.is_some()
                || position + 2 >= tokens.len()
                || !tokens[position + 1].eq_ignore_ascii_case("Oct")
            {
                return None;
            }

            bw_oct = Some(tokens[position + 2].parse::<f64>().ok()?);
            position += 3;
        } else {
            return None;
        }
    }

    Some(FilterLine::Biquad {
        enabled,
        ftype,
        freq_hz,
        gain_db,
        q,
        bw_oct,
        index,
    })
}

fn parse_graphic_eq(line: &str) -> Option<FilterLine> {
    let (head, body) = line.split_once(':')?;

    if !head.trim().eq_ignore_ascii_case("GraphicEQ") {
        return None;
    }

    let mut points = Vec::new();

    for segment in body.split(';') {
        let tokens: Vec<&str> = segment.split_whitespace().collect();

        if tokens.is_empty() {
            continue;
        }

        if tokens.len() != 2 {
            return None;
        }

        let frequency = tokens[0].parse::<f64>().ok()?;
        let gain = tokens[1].parse::<f64>().ok()?;

        points.push([frequency, gain]);
    }

    Some(FilterLine::GraphicEq { points })
}

fn is_supported_filter_type(ftype: &str) -> bool {
    matches!(
        ftype,
        "PK" | "LP" | "HP" | "LPQ" | "HPQ" | "BP" | "LS" | "HS" | "LSC" | "HSC" | "NO" | "AP"
    )
}

fn format_float(value: f64) -> String {
    value.to_string()
}

fn serialize_line(line: &FilterLine) -> String {
    match line {
        FilterLine::Preamp { gain_db } => {
            format!("Preamp: {} dB", format_float(*gain_db))
        }
        FilterLine::Biquad {
            enabled,
            ftype,
            freq_hz,
            gain_db,
            q,
            bw_oct,
            index,
        } => {
            let mut output = match index {
                Some(index) => format!("Filter {}:", index),
                None => "Filter:".to_owned(),
            };

            output.push_str(&format!(
                " {} {} Fc {} Hz",
                if *enabled { "ON" } else { "OFF" },
                ftype.to_ascii_uppercase(),
                format_float(*freq_hz)
            ));

            if let Some(value) = gain_db {
                output.push_str(" Gain ");
                output.push_str(&format_float(*value));
                output.push_str(" dB");
            }

            if let Some(value) = q {
                output.push_str(" Q ");
                output.push_str(&format_float(*value));
            }

            if let Some(value) = bw_oct {
                output.push_str(" BW Oct ");
                output.push_str(&format_float(*value));
            }

            output
        }
        FilterLine::GraphicEq { points } => {
            if points.is_empty() {
                "GraphicEQ:".to_owned()
            } else {
                let serialized_points = points
                    .iter()
                    .map(|point| format!("{} {}", format_float(point[0]), format_float(point[1])))
                    .collect::<Vec<_>>()
                    .join("; ");

                format!("GraphicEQ: {}", serialized_points)
            }
        }
        FilterLine::Comment { text } => format!("# {}", text),
        FilterLine::Blank => String::new(),
        FilterLine::Raw { text } => text.clone(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_multiline_config() {
        let input = concat!(
            "# toy config\n",
            "Preamp: -6.5 dB\n",
            "Filter 1: ON PK Fc 1000 Hz Gain -3 dB Q 1.41\n",
            "Filter: ON HP Fc 80 Hz\n",
            "Filter: ON LSC Fc 100 Hz Gain 3 dB Q 0.7\n",
            "GraphicEQ: 25 -3; 40 -2; 63 0\n",
            "\n",
            "Copy: L=R"
        );

        let lines = parse_config(input);

        assert_eq!(lines.len(), 8);
        assert_eq!(
            lines[0],
            FilterLine::Comment {
                text: "toy config".to_owned()
            }
        );
        assert_eq!(lines[1], FilterLine::Preamp { gain_db: -6.5 });
        assert_eq!(
            lines[5],
            FilterLine::GraphicEq {
                points: vec![[25.0, -3.0], [40.0, -2.0], [63.0, 0.0]]
            }
        );
        assert_eq!(lines[6], FilterLine::Blank);
        assert_eq!(
            lines[7],
            FilterLine::Raw {
                text: "Copy: L=R".to_owned()
            }
        );

        let serialized = serialize_config(&lines);
        assert_eq!(serialized, input);
        assert_eq!(parse_config(&serialized), lines);
    }

    #[test]
    fn gain_and_q_are_optional() {
        let lines = parse_config(
            "Filter: ON HP Fc 80 Hz\n\
             Filter: ON PK Fc 100 Hz Q 1.41\n\
             Filter: ON PK Fc 200 Hz Gain -2 dB\n",
        );

        assert_eq!(
            lines,
            vec![
                FilterLine::Biquad {
                    enabled: true,
                    ftype: "HP".to_owned(),
                    freq_hz: 80.0,
                    gain_db: None,
                    q: None,
                    bw_oct: None,
                    index: None,
                },
                FilterLine::Biquad {
                    enabled: true,
                    ftype: "PK".to_owned(),
                    freq_hz: 100.0,
                    gain_db: None,
                    q: Some(1.41),
                    bw_oct: None,
                    index: None,
                },
                FilterLine::Biquad {
                    enabled: true,
                    ftype: "PK".to_owned(),
                    freq_hz: 200.0,
                    gain_db: Some(-2.0),
                    q: None,
                    bw_oct: None,
                    index: None,
                },
            ]
        );
    }

    #[test]
    fn parses_filter_index() {
        let lines = parse_config("filter 3: off lsc fc 100 hz gain 3 db q 0.7");

        assert_eq!(
            lines,
            vec![FilterLine::Biquad {
                enabled: false,
                ftype: "LSC".to_owned(),
                freq_hz: 100.0,
                gain_db: Some(3.0),
                q: Some(0.7),
                bw_oct: None,
                index: Some(3),
            }]
        );
    }

    #[test]
    fn comments_and_raw_lines_preserve_their_models() {
        let lines = parse_config("  # hello\n  cOpY: L=R  ");

        assert_eq!(
            lines[0],
            FilterLine::Comment {
                text: "hello".to_owned()
            }
        );
        assert_eq!(
            lines[1],
            FilterLine::Raw {
                text: "  cOpY: L=R  ".to_owned()
            }
        );
        assert_eq!(parse_config(&serialize_config(&lines)), lines);
    }
}
