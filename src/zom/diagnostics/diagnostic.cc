#include "src/zom/diagnostics/diagnostic.h"

namespace zom {
namespace diagnostics {

Diagnostic::Diagnostic(DiagnosticSeverity severity, std::string message,
                       zom::source::SourceLoc location)
    : severity(severity),
      message(zc::mv(message)),
      location(zc::mv(location)) {}

DiagnosticSeverity Diagnostic::getSeverity() const { return severity; }
const std::string& Diagnostic::getMessage() const { return message; }
const zom::source::SourceLoc& Diagnostic::getSourceLoc() const {
  return location;
}
const std::vector<std::string>& Diagnostic::getNotes() const { return notes; }

void Diagnostic::addNote(std::string note) { notes.push_back(zc::mv(note)); }

}  // namespace diagnostics
}  // namespace zom