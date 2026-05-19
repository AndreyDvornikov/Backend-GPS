#include <atomic>
#include <thread>

#include "Database.h"
#include "GuiApp.h"
#include "TelemetryModel.h"
#include "TelemetryServer.h"

int main() {
    std::atomic<bool> running{true};
    Database database;
    TelemetryModel model;

    model.SetDatabaseReady(database.Init());

    TelemetryServer server(database, model, running);
    std::thread listener(&TelemetryServer::Run, &server);

    RunGui(running, database, model);

    running = false;
    if (listener.joinable()) {
        listener.join();
    }

    return 0;
}
