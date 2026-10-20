import { createRoot } from "react-dom/client";
import App from "./App";
import "./styles.css";

// No StrictMode: its double-run of effects in development would open two
// Home Assistant connections and boot the simulator twice.
createRoot(document.getElementById("root")!).render(<App />);
