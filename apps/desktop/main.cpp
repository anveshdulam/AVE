#include <iostream>

#if defined(AVE_ENABLE_QT)
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#endif

int main(int argc, char *argv[]) {
  std::cout << "Autonomous Video Editor Desktop Application Shell\n";

#if defined(AVE_ENABLE_QT)
  QGuiApplication app(argc, argv);
  app.setApplicationName("Autonomous Video Editor");
  app.setOrganizationName("AVE");

  QQmlApplicationEngine engine;
  // Initial minimal window load or QQuickWindow
  return app.exec();
#else
  (void)argc;
  (void)argv;
  std::cout
      << "Built without Qt presentation layer (headless/bootstrap mode).\n";
  return 0;
#endif
}
