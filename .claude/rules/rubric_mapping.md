# Report Structure & Rubric Mapping

The report must follow this 14-section structure to satisfy the ESE Capstone Guidelines and Rubric. When editing a specific file, apply the corresponding rubric criteria.

## File: 01_Intro_and_LitReview.md (Sections 1 & 2)
- **Rubric ED1 (Problem Definition & Research):** Must explicitly state the problem, the rationale, and compare PortaPill against existing solutions (Tier 2/Tier 3). Must highlight the dual-sensor closed-loop verification as the primary differentiator.

## File: 02_Requirements_and_Design.md (Sections 3 & 4)
- **Rubric PA2 (Methodology):** Must justify the system breakdown into testable components.
- **Rubric ED2 (Preliminary Design):** Must show how the problem was decomposed into functional units (Power, Control, Sensing, Mechanical, UI).
- **Must Include:** MoSCoW scope and SMART goal criteria.

## File: 03_Functional_Units.md (Section 5)
- **Rubric ED3 (Detailed Design - 20 pts):** This is a heavy grading section. Every functional unit must have: Definition, Principle of Operation, Design Specification, Engineering Principles/Analysis, Engineering Tools, and Test Requirements. Must reference schematics/flowcharts.

## File: 04_Methodology_and_Implementation.md (Sections 6 & 7)
- **Rubric ED4 (Implementation - 20 pts):** Must document the progression from the perfboard prototype to the Altium-designed custom PCB. Focus on the resolution of firmware defects (e.g., I2S audio power coupling, UART concurrency issues).

## File: 05_Results_and_Validation.md (Sections 8 & 9)
- **Rubric IV2 (Data Collection & Analysis - 40 pts):** The largest rubric criteria. Must contain tables of measured values (e.g., 3.3V rail stability, battery runtime, dispense reliability percentage). Do not write vague results; use hard numbers.
- **Rubric PA3 (Validation - 10 pts):** Section 9.4 must honestly evaluate measurement uncertainty, systematic errors, and limitations (e.g., testing with candy instead of real pills).

## File: 06_Impact_Ethics_Conclusion.md (Sections 10 - 14)
- **Rubric SC1 (Impact on Society - 10 pts):** Section 10. Must address environmental, societal, and safety hazards (e.g., Li-ion battery risks).
- **Rubric EE1 (Ethical Responsibility - 10 pts):** Section 11. Must address professional obligations, regulatory context (Health Canada classification), and unauthenticated API risks.
- **Rubric LL2 (Lifelong Learning - 20 pts):** Section 12. Must explicitly detail new skills acquired (e.g., ESP-IDF FreeRTOS, Altium PCB design) and how they were applied.
- **Rubric CM2 (Communication - 20 pts):** Section 14. Conclusion must accurately assess success without overclaiming.
