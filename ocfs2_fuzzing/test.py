# Импортируем библиотеки
import matplotlib.pyplot as plt
import networkx as nx

# Создаём граф
G = nx.DiGraph()

# Определяем узлы
nodes = {
    "Syzkaller": "",
    "Узел 1": "",
    "Узел 2": "",
    "OCFS2_1": "",
    "OCFS2_2": "",
    "DLM": "",
    "Heartbeat": "",
    "DRBD1": "",
    "DRBD2": "",
    "/dev/drbd1": "",
    "/dev/drbd2": ""
}

# Добавляем узлы в граф
G.add_nodes_from(nodes.keys())

# Определяем связи (рёбра) с подписями
edges = {
    ("Syzkaller", "Узел 1"): "Генерация системных вызовов",
    ("Узел 1", "OCFS2_1"): "",
    ("Узел 2", "OCFS2_2"): "",
    ("OCFS2_1", "DLM"): "Запрос блокировки",
    ("OCFS2_2", "DLM"): "Запрос блокировки",
    ("OCFS2_1", "Heartbeat"): "Мониторинг",
    ("OCFS2_2", "Heartbeat"): "Мониторинг",
    ("OCFS2_1", "DRBD1"): "",
    ("OCFS2_2", "DRBD2"): "",
    ("DRBD1", "DRBD2"): "Репликация",
    ("DRBD2", "DRBD1"): "Репликация",
    ("DRBD1", "/dev/drbd1"): "",
    ("DRBD2", "/dev/drbd2"): ""
}

# Добавляем связи в граф
G.add_edges_from(edges.keys())

# Определяем координаты узлов
pos = {
    "Syzkaller": (3, 8),
    "Узел 1": (-3, 7), "Узел 2": (9, 7),
    "OCFS2_1": (-3, 5.5), "OCFS2_2": (9, 5.5),
    "DLM": (3, 6.5),
    "Heartbeat": (3, 4.5),
    "DRBD1": (-3, 3.5), "DRBD2": (9, 3.5),
    "/dev/drbd1": (-3, 2), "/dev/drbd2": (9, 2)
}


# Рисуем граф
fig = plt.figure(figsize=(12, 8), facecolor='#f9fdfe')
ax = fig.add_subplot(111)
ax.set_facecolor('#f9fdfe') 
nx.draw(G, pos, with_labels=True, node_size=5500, node_color="lightblue",
        edge_color="black", font_size=12, font_weight="bold", arrows=True, ax = ax)

# Добавляем подписи узлов
labels = {node: nodes[node] for node in G.nodes()}
nx.draw_networkx_labels(G, pos, labels, font_size=12, font_weight="bold")

# Добавляем подписи на стрелках (связях)
edge_labels = {(u, v): edges[(u, v)] for u, v in G.edges()}
nx.draw_networkx_edge_labels(G, pos, edge_labels=edge_labels, font_size=12, font_color="green")

# Добавляем заголовок
plt.title("Исправленная схема OCFS2 + DRBD с подписями на стрелках")

# Показываем результат
plt.show()

plt.savefig("ocfs2_drbd_diagram.svg", format="svg")  # Сохранение в SVG
plt.savefig("ocfs2_drbd_diagram.pdf", format="pdf")  # Сохранение в PDF
plt.savefig("ocfs2_drbd_diagram.eps", format="eps")  # Сохранение в EPS

