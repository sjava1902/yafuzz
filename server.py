from flask import Flask, render_template_string, request
import os

app = Flask(__name__)

# Пути к файлам
COVERAGE_FILE = "mapped_coverage.txt"  # Результат addr2line
SOURCE_ROOT = "/home/slava/linux/"  # Корень исходников ядра

HTML_TEMPLATE = """
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <title>{{ title }}</title>
    <style>
        body {
            font-family: Arial, sans-serif;
        }
        ul {
            list-style-type: none;
        }
        li {
            margin: 5px 0;
        }
        .directory {
            font-weight: bold;
        }
        .covered {
            background-color: lightcoral;
        }
        pre {
            white-space: pre-wrap;
            background-color: #f8f8f8;
            padding: 10px;
            border: 1px solid #ddd;
        }
    </style>
</head>
<body>
    <h1>{{ title }}</h1>
    <div id="file-tree">
        {{ file_tree|safe }}
    </div>
    {% if file_content %}
        <h2>Файл: {{ file_name }}</h2>
        <pre>{{ file_content|safe }}</pre>
    {% endif %}
</body>
</html>
"""

# Функция для загрузки покрытия
def load_coverage(coverage_file):
    coverage_map = {}
    with open(coverage_file, "r") as f:
        lines = f.readlines()
        for i in range(0, len(lines), 3):  # Читаем блоками по 3 строки
            if i + 2 < len(lines):
                address = lines[i].strip()  # Адрес (можем игнорировать)
                function = lines[i + 1].strip()  # Имя функции
                source_line = lines[i + 2].strip()  # Путь и строка

                if ":" in source_line:  # Проверяем, содержит ли строка ":"
                    try:
                        # Разбиваем путь и строку
                        file_path, line_info = source_line.rsplit(":", 1)
                        line_number = int(line_info.split(" ")[0])

                        if os.path.exists(file_path):  # Проверяем существование файла
                            if file_path not in coverage_map:
                                coverage_map[file_path] = set()
                            coverage_map[file_path].add(line_number)
                    except ValueError as e:
                        print(f"[WARN] Пропуск некорректной строки: {source_line} ({e})")
    return coverage_map

# Загрузка покрытия в память
COVERAGE_MAP = load_coverage(COVERAGE_FILE)

# Генерация HTML-дерева директорий
def generate_html_tree(root_dir, current_dir=None):
    if current_dir is None:
        current_dir = root_dir

    html = "<ul>"
    for entry in sorted(os.listdir(current_dir)):
        entry_path = os.path.join(current_dir, entry)

        if os.path.isdir(entry_path):
            # Проверяем, есть ли покрытые файлы или директории внутри
            if any(file.startswith(entry_path) for file in COVERAGE_MAP):
                html += f'<li><a href="/navigate?dir={entry_path}" class="directory">{entry}/</a></li>'
        elif os.path.isfile(entry_path):
            if entry_path in COVERAGE_MAP:  # Проверяем, есть ли покрытие для файла
                html += f'<li><a href="/view?file={entry_path}">{entry}</a></li>'
    html += "</ul>"
    return html

# Главная страница
@app.route("/")
def index():
    file_tree = generate_html_tree(SOURCE_ROOT)
    return render_template_string(
        HTML_TEMPLATE,
        title="Навигация по директориям (Только покрытые)",
        file_tree=file_tree,
        file_content=None,
        file_name=None,
    )

# Навигация по директориям
@app.route("/navigate")
def navigate():
    directory = request.args.get("dir")
    if not directory or not os.path.isdir(directory):
        return "Директория не найдена", 404

    if not directory.startswith(SOURCE_ROOT):
        return "Доступ к директории запрещен", 403

    file_tree = generate_html_tree(SOURCE_ROOT, directory)
    return render_template_string(
        HTML_TEMPLATE,
        title=f"Навигация: {directory}",
        file_tree=file_tree,
        file_content=None,
        file_name=None,
    )

# Отображение исходного файла с подсветкой покрытия
@app.route("/view")
def view_source():
    file_path = request.args.get("file")
    if not file_path or not os.path.isfile(file_path):
        return "Файл не найден", 404

    if not file_path.startswith(SOURCE_ROOT):
        return "Доступ к файлу запрещен", 403

    # Загрузка исходного файла
    with open(file_path, "r") as f:
        lines = f.readlines()

    # Определение строк покрытия
    covered_lines = COVERAGE_MAP.get(file_path, set())

    # Генерация HTML с сохранением форматирования
    highlighted_lines = []
    for i, line in enumerate(lines, start=1):
        if i in covered_lines:
            highlighted_lines.append(f'<span class="covered">{line}</span>')
        else:
            highlighted_lines.append(line)

    # Присоединение строк для отображения
    code_html = "".join(highlighted_lines)

    return render_template_string(
        HTML_TEMPLATE,
        title=f"Исходный код: {os.path.basename(file_path)}",
        file_tree=None,
        file_content=f"<pre>{code_html}</pre>",  # Использование <pre> для сохранения отступов
        file_name=os.path.basename(file_path),
    )


if __name__ == "__main__":
    app.run(debug=True, host="0.0.0.0", port=5000)
