# Stress Test

## Setup

0. Deploy MySQL for testing

    ```sh
    docker compose up -d
    ```

1. Declare the database URL

    ```sh
    export DATABASE_URL="mysql://root:passwd@localhost/stress_test"
    ```

2. Create the database.

    ```sh
    # cargo install sqlx-cli
    sqlx db create
    ```

3. Run sql migrations

    ```sh
    sqlx migrate run
    ```

## Usage

Add a todo 

```sh
cargo run -- add "todo description"
```

Complete a todo.

```sh
cargo run -- done <todo id>
```

List all todos

```sh
cargo run
```