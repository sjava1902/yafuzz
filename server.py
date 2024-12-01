from flask import Flask, render_template, request
import os

app = Flask(__name__)

# Пути к файлам
COVERAGE_FILE = "mapped_coverage.txt"  # Результат addr2line
SOURCE_ROOT = "/home/slava/linux/"  # Корень исходников ядра

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
                        
                        # Убираем дискриминаторы, если есть
                        line_number = line_info.split(" ")[0]
                        line_number = int(line_number)

                        if os.path.exists(file_path):  # Проверяем существование файла
                            if file_path not in coverage_map:
                                coverage_map[file_path] = set()
                            coverage_map[file_path].add(line_number)
                    except ValueError as e:
                        print(f"[WARN] Пропуск некорректной строки: {source_line} ({e})")
    return coverage_map


# Загрузка покрытия в память
COVERAGE_MAP = load_coverage(COVERAGE_FILE)

# Отображение исходного файла с подсветкой покрытия
@app.route("/view")
def view_source():
    file_path = request.args.get("file")
    if not file_path or not os.path.isfile(file_path):
        return "Файл не найден", 404

    # Относительный путь для безопасности
    if not file_path.startswith(SOURCE_ROOT):
        return "Доступ к файлу запрещен", 403

    # Загрузка исходного файла
    with open(file_path, "r") as f:
        lines = f.readlines()

    # Определение строк покрытия
    covered_lines = COVERAGE_MAP.get(file_path, set())

    # Генерация HTML
    highlighted_lines = []
    for i, line in enumerate(lines, start=1):
        if i in covered_lines:
            highlighted_lines.append(f'<span style="background-color: lightcoral;">{line.strip()}</span>')
        else:
            highlighted_lines.append(line.strip())

    # Присоединение строк для отображения
    code_html = "<br>".join(highlighted_lines)

    return f"""
    <html>
    <head>
        <title>Исходный код: {os.path.basename(file_path)}</title>
    </head>
    <body>
        <h1>Исходный код: {os.path.basename(file_path)}</h1>
        <pre style="font-family: monospace;">{code_html}</pre>
    </body>
    </html>
    """

# Главная страница
@app.route("/")
def index():
    files = list(COVERAGE_MAP.keys())
    file_links = [f'<li><a href="/view?file={file}">{file}</a></li>' for file in files]
    return f"""
    <html>
    <head>
        <title>Список файлов покрытия</title>
    </head>
    <body>
        <h1>Файлы покрытия</h1>
        <ul>
            {''.join(file_links)}
        </ul>
    </body>
    </html>
    """

if __name__ == "__main__":
    app.run(debug=True, host="0.0.0.0", port=5000)
