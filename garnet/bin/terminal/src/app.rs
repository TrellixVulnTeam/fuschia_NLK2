// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::terminal_view::TerminalViewAssistant,
    carnelian::{AppAssistant, ViewAssistantPtr, ViewKey, ViewMode},
    failure::Error,
};

pub struct TerminalAssistant;

impl TerminalAssistant {
    pub fn new() -> TerminalAssistant {
        TerminalAssistant {}
    }
}

impl AppAssistant for TerminalAssistant {
    fn setup(&mut self) -> Result<(), Error> {
        Ok(())
    }

    fn create_view_assistant_canvas(&mut self, _: ViewKey) -> Result<ViewAssistantPtr, Error> {
        Ok(Box::new(TerminalViewAssistant::new()))
    }

    fn get_mode(&self) -> ViewMode {
        ViewMode::Canvas
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    #[test]
    fn app_runs_in_canvas_mode() {
        let app = TerminalAssistant::new();
        assert_eq!(app.get_mode(), ViewMode::Canvas);
    }

    #[fasync::run_singlethreaded(test)]
    async fn creates_terminal_view() -> Result<(), Error> {
        let mut app = TerminalAssistant::new();
        app.create_view_assistant_canvas(1)?;
        Ok(())
    }
}
