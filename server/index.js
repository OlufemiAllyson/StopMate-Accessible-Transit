const express = require("express");
const path = require("path");

const app = express();
const PORT = process.env.PORT || 3000;

app.use(express.static(path.join(__dirname, "public")));

//MOCK DATA
const MOCK_STOPS = {
  "demo-stop-1": {
    stopId: "demo-stop-1",
    stopName: "University Ave & 13th St",
    nextBuses: [
      { route: "20", minutesAway: 4 },
      { route: "75", minutesAway: 12 },
      { route: "9", minutesAway: 21 },
    ],
  },
};

app.get("/stop/:id", (req, res) => {
  const stop = MOCK_STOPS[req.params.id];
  if (!stop) {
    return res.status(404).json({ error: "Stop not found" });
  }
  res.json(stop);
});

app.listen(PORT, () => {
  console.log(`StopMate server running at http://localhost:${PORT}`);
});