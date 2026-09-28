"""
TomTom Face Studio - Declarative Rule Evaluator
===============================================
Evaluates signal conditions and logic gates to determine element visibility.
Keeps device runtime deterministic without executing arbitrary code.
"""

from typing import Dict, Any, List

SUPPORTED_SIGNALS = {
    "battery_percent",
    "battery_low",
    "gps_status",
    "gps_fix",
    "weather_status",
    "weather_unavailable",
    "charging"
}

SUPPORTED_OPERATORS = {"==", "!=", "<=", ">=", "<", ">"}


class RuleEngine:
    """Evaluates rule conditions against simulated device state."""

    DEFAULT_STATE = {
        "battery_percent": 100,
        "battery_low": False,
        "gps_status": 1,
        "gps_fix": True,
        "weather_status": 1,
        "weather_unavailable": False,
        "charging": False
    }

    @staticmethod
    def compute_state(state_input: Dict[str, Any]) -> Dict[str, Any]:
        """Derive secondary boolean flags from primary state inputs."""
        state = dict(RuleEngine.DEFAULT_STATE)
        if isinstance(state_input, dict):
            state.update(state_input)

        b_pct = state.get("battery_percent", 100)
        state["battery_low"] = (b_pct <= 20)

        gps_st = state.get("gps_status", 1)
        state["gps_fix"] = (gps_st == 1)

        w_st = state.get("weather_status", 1)
        state["weather_unavailable"] = (w_st == 0)

        return state

    @staticmethod
    def is_element_visible(element: Dict[str, Any], rules_def: List[Dict[str, Any]], current_state: Dict[str, Any]) -> bool:
        """
        Check if an element should be rendered given its rule identifier and current state.
        If element has no rule (None or empty string), it is always visible.
        If rule is unknown, invalid, or malformed, returns False (do not silently show!).
        """
        if not isinstance(element, dict):
            return False

        rule_name = element.get("rule")
        if not rule_name:  # None or empty string -> no rule bound
            return True

        if not isinstance(rule_name, str):
            return False

        state = RuleEngine.compute_state(current_state)

        # Check standard pre-built rule names directly
        if rule_name == "battery_low":
            return bool(state.get("battery_low", False))
        elif rule_name == "gps_fix":
            return bool(state.get("gps_fix", True))
        elif rule_name == "weather_unavailable":
            return bool(state.get("weather_unavailable", False))
        elif rule_name in state and isinstance(state[rule_name], bool):
            return bool(state[rule_name])

        # Match named rule in rules_def list
        if not isinstance(rules_def, list):
            return False

        for r in rules_def:
            if isinstance(r, dict) and r.get("name") == rule_name:
                sig = r.get("signal")
                op = r.get("op", "==")
                val = r.get("value")

                if not sig or sig not in SUPPORTED_SIGNALS or op not in SUPPORTED_OPERATORS:
                    return False  # Invalid rule signal/operator -> Fail closed (False)

                cur_val = state.get(sig)
                if cur_val is None:
                    return False  # Unknown/missing signal value -> Fail closed (False)

                try:
                    if op == "==":
                        return cur_val == val
                    elif op == "!=":
                        return cur_val != val
                    elif op == "<=":
                        return cur_val <= val
                    elif op == ">=":
                        return cur_val >= val
                    elif op == "<":
                        return cur_val < val
                    elif op == ">":
                        return cur_val > val
                except TypeError:
                    return False  # Incompatible comparison types -> Fail closed

                return False

        # If rule_name was not found in rules_def or built-ins -> Return False (Fail closed!)
        return False
