#include "ProcessLauncher.h"

#include <QDebug>
#include <QFileInfo>
#include <QRegularExpression>


ProcessLauncher::ProcessLauncher(QObject* parent)
    : QObject(parent)
    , process(nullptr)
{}

void ProcessLauncher::launchGame(const Model::Platform* platform, const Model::Game* game)
{
    static const auto SEPARATOR = "----------------------------------------";

    const QString launch_cmd = createLaunchCommand(platform, game);

    qInfo().noquote() << SEPARATOR;
    qInfo().noquote() << tr("Executing command: `%1`").arg(launch_cmd);

    runProcess(launch_cmd);

    emit processFinished();
}

QString ProcessLauncher::createLaunchCommand(const Model::Platform* platform, const Model::Game* game)
{
    enum class ParamType : unsigned char {
        PATH,
        BASENAME,
    };

    QMap<ParamType, QString> params = {
        { ParamType::PATH, game->m_rom_path },
        { ParamType::BASENAME, QFileInfo(game->m_rom_path).completeBaseName() },
    };

    // Prepare the parameters for QProcess
    const QRegularExpression WHITESPACE_REGEX("\\s");
    for (auto& param : params) {
        // QProcess: Literal quotes are represented by triple quotes
        param.replace('"', "\"\"\"");
        // QProcess: Arguments containing spaces must be quoted
        if (param.contains(WHITESPACE_REGEX))
            param.prepend('"').append('"');

        qDebug().noquote() << param;
    }

    // replace known keywords
    QString launch_cmd = platform->m_launch_cmd;
    // first, replace manually quoted elements in the command string
    launch_cmd
        .replace("\"%ROM%\"", params.value(ParamType::PATH))
        .replace("\"%ROM_RAW%\"", params.value(ParamType::PATH))
        .replace("\"%BASENAME%\"", params.value(ParamType::BASENAME));
    // then replace the unquoted forms
    launch_cmd
        .replace("%ROM%", params.value(ParamType::PATH))
        .replace("%ROM_RAW%", params.value(ParamType::PATH))
        .replace("%BASENAME%", params.value(ParamType::BASENAME));

    return launch_cmd;
}

void ProcessLauncher::runProcess(const QString& command)
{
    Q_ASSERT(!process);
    process = new QProcess();

    // set up signals and slots
    connect(process.data(), &QProcess::started, this, &ProcessLauncher::onProcessStarted);
    connect(process.data(), &QProcess::errorOccurred, this, &ProcessLauncher::onProcessFailed);
    connect(process.data(), static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &ProcessLauncher::onProcessFinished);

    // run the command
    process->start(command, QProcess::ReadOnly);

    // wait
    process->waitForFinished();
    process->terminate(); // TODO: `terminate` is probably unnecessary
    process->deleteLater();
}

void ProcessLauncher::onProcessStarted()
{
    Q_ASSERT(process);
    qInfo().noquote() << tr("Process %1 started").arg(process->processId());
}

void ProcessLauncher::onProcessFailed(QProcess::ProcessError error)
{
    Q_ASSERT(process);
    switch (error) {
        case QProcess::FailedToStart:
            qWarning().noquote() << tr("Could not run the command `%1`; either the"
                                       " invoked program is missing, or you don't have"
                                       " the permission to run it.")
                                    .arg(process->program());
            break;
        case QProcess::Crashed:
            qWarning().noquote() << tr("The external program `%1` has crashed")
                                    .arg(process->program());
            break;
        case QProcess::Timedout:
            qWarning().noquote() << tr("The command `%1` has not started in a"
                                       " reasonable amount of time")
                                    .arg(process->program());
            break;
        case QProcess::ReadError:
        case QProcess::WriteError:
            // We don't communicate with the launched process at the moment
            Q_UNREACHABLE();
            break;
        default:
            qWarning().noquote() << tr("Running the command `%1` failed due to an unknown error")
                                    .arg(process->program());
            break;
    }
}

void ProcessLauncher::onProcessFinished(int exitcode, QProcess::ExitStatus exitstatus)
{
    Q_ASSERT(process);
    switch (exitstatus) {
        case QProcess::NormalExit:
            qInfo().noquote() << tr("The external program has finished cleanly, with exit code %2")
                                 .arg(exitcode);
            break;
        case QProcess::CrashExit:
            qInfo().noquote() << tr("The external program has crashed on exit, with exit code %2")
                                 .arg(exitcode);
        default:
            // If you reach this branch, there was an API change in Qt
            Q_UNREACHABLE();
            break;
    }
}